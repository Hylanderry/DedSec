// DedSec Replay Engine — Xbox D3D12.X command list → PC D3D12 translation
// Translates recorded IR commands into native PC D3D12 calls with error recovery,
// batch submission and fence tracking. Thread pool for parallel decode.
// Original DedSec implementation; not copied from XCL or Leonida.
//
// Signed-off-by: DedSec <hylanderry@users.noreply.github.com>

#pragma once

#include "command_list.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// Forward declarations — no d3d12.h dependency in the header.
// The .cpp includes windows.h + d3d12.h and casts from ResourceId.
struct ID3D12Device;
struct ID3D12GraphicsCommandList;
struct ID3D12CommandQueue;
struct ID3D12Fence;

namespace dedsec {

// Callback for error reporting without crashing the game.
// severity: 0 = warning, 1 = error, 2 = fatal
using ReplayErrorCallback = std::function<void(int severity, const char* command_name, const char* message)>;

// Statistics tracked per replay session.
struct ReplayStats {
    std::uint64_t commands_replayed{0};
    std::uint64_t commands_failed{0};
    std::uint64_t draw_calls{0};
    std::uint64_t dispatches{0};
    std::uint64_t barriers{0};
    std::uint64_t copies{0};
    std::uint64_t clears{0};
    std::uint64_t pso_binds{0};
    std::uint64_t root_sig_binds{0};
    double last_frame_ms{0.0};
};

// Configuration for the replay engine.
struct ReplayConfig {
    // Maximum number of worker threads for parallel command list decode.
    // 0 = auto-detect from hardware concurrency.
    std::uint32_t worker_threads{0};

    // Maximum command lists per batch submission.
    std::uint32_t max_batch_size{16};

    // Whether to continue on error (true) or abort replay on first failure (false).
    bool continue_on_error{true};

    // Whether to log every failed command via the error callback.
    bool log_failures{true};
};

// A single command list ready for replay. Holds the IR and optional metadata.
struct ReplayCommandList {
    CommandList ir;
    std::uint64_t id{0};         // unique identifier for tracking
    std::string label;           // debug label
    bool compute_only{false};    // true if this CL only contains compute commands
};

// Batch of command lists submitted together.
struct ReplayBatch {
    std::vector<std::size_t> cl_indices;  // indices into the engine's CL array
    std::uint64_t fence_value{0};         // fence value signaled after GPU completion
};

// The replay engine: translates Xbox IR → PC D3D12 and manages submission.
class ReplayEngine {
public:
    ReplayEngine();
    ~ReplayEngine();

    // Non-copyable, non-movable (owns threads and GPU resources).
    ReplayEngine(const ReplayEngine&) = delete;
    ReplayEngine& operator=(const ReplayEngine&) = delete;

    // Initialize with a PC D3D12 device and command queue. Must be called before any replay.
    // Returns false on failure.
    bool init(ID3D12Device* device, ID3D12CommandQueue* queue, const ReplayConfig& config = {});

    // Shutdown: stop workers, release internal resources. Safe to call multiple times.
    void shutdown();

    // Set the error callback. Default is a no-op.
    void set_error_callback(ReplayErrorCallback cb);

    // --- Command List Management ---

    // Add a command list for replay. Returns its index.
    std::size_t add_command_list(ReplayCommandList cl);

    // Remove all command lists (call between frames or on level load).
    void clear_command_lists();

    // Number of registered command lists.
    std::size_t command_list_count() const;

    // --- Replay ---

    // Replay a single command list by index onto an existing PC command list.
    // Returns false if any command failed (and continue_on_error is false).
    bool replay(std::size_t cl_index, ID3D12GraphicsCommandList* pc_cl);

    // Replay all registered command lists using internal batching.
    // Allocates PC command lists from the provided allocator (as raw pointer, cast internally).
    // Signals the fence after GPU work completes. Returns the fence value.
    std::uint64_t replay_all(ID3D12CommandQueue* queue_override = nullptr);

    // Wait for all submitted GPU work to complete.
    void wait_for_idle();

    // --- Statistics ---

    ReplayStats stats() const;
    void reset_stats();

    // --- Resource Mapping ---

    // Map an Xbox ResourceId to a PC D3D12 resource pointer.
    // The replay engine uses this to resolve ResourceId references in commands.
    using ResourceResolver = std::function<void*(ResourceId)>;
    void set_resource_resolver(ResourceResolver resolver);

    // Map an Xbox PSO handle to a PC ID3D12PipelineState*.
    using PsoResolver = std::function<void*(ResourceId)>;
    void set_pso_resolver(PsoResolver resolver);

    // Map an Xbox root signature handle to a PC ID3D12RootSignature*.
    using RootSigResolver = std::function<void*(ResourceId)>;
    void set_root_sig_resolver(RootSigResolver resolver);

    // Map an Xbox descriptor heap handle to a PC ID3D12DescriptorHeap*.
    using HeapResolver = std::function<void*(ResourceId)>;
    void set_heap_resolver(HeapResolver resolver);

private:
    struct Impl;
    Impl* impl_;
};

} // namespace dedsec