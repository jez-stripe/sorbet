#ifndef SORBET_LSP_TYPECHECKEPOCHMANAGER_H
#define SORBET_LSP_TYPECHECKEPOCHMANAGER_H

#include "core/core.h"
#include <memory>

namespace sorbet::core::lsp {
class PreemptionTaskManager;
class TypecheckEpochManager final {
public:
    struct TypecheckingStatus {
        bool slowPathRunning;
        bool slowPathWasCanceled;
        u4 lastCommittedEpoch;
        u4 epoch;
    };

private:
    // Used to linearize operations involving lastCommittedLSPEpoch.
    mutable absl::Mutex epochMutex;
    // Contains the current edit version (epoch) that the processing thread is typechecking or has
    // typechecked last. Is bumped by the typechecking thread.
    std::atomic<u4> currentlyProcessingLSPEpoch GUARDED_BY(epochMutex);
    // Should always be `>= currentlyProcessingLSPEpoch` (modulo overflows).
    // If value in `lspEpochInvalidator` is different from `currentlyProcessingLSPEpoch`, then LSP wants the current
    // request to be cancelled. Is bumped by the preprocessor thread (which determines cancellations).
    std::atomic<u4> lspEpochInvalidator GUARDED_BY(epochMutex);
    // Should always be >= currentlyProcessingLSPEpoch. Is bumped by the typechecking thread.
    // Contains the versionEnd of the last committed slow path.
    // If lastCommittedLSPEpoch != currentlyProcessingLSPEpoch, then GlobalState is currently running a slow path
    // containing edits (lastCommittedLSPEpoch, currentlyProcessingLSPEpoch].
    std::atomic<u4> lastCommittedLSPEpoch GUARDED_BY(epochMutex);
    // Thread ID of the typechecking thread. Lazily set.
    mutable std::optional<std::thread::id> typecheckingThreadId;
    // Thread ID of the preprocess thread. Lazily set.
    mutable std::optional<std::thread::id> preprocessThreadId;

    TypecheckingStatus getStatusInternal() const EXCLUSIVE_LOCKS_REQUIRED(epochMutex);

public:
    static void assertConsistentThread(std::optional<std::thread::id> &expectedThreadId, std::string_view method,
                                       std::string_view threadName);
    // Indicates an intent to begin committing a specific epoch.
    // Run only from the message processing thread.
    // TODO(jvilk): This responsibility will move to the typechecking thread.
    void startCommitEpoch(u4 fromEpoch, u4 toEpoch);
    // Returns 'true' if the currently running typecheck run has been canceled.
    bool wasTypecheckingCanceled() const;
    // Retrieve the status of typechecking.
    TypecheckingStatus getStatus() const;
    // Tries to cancel a running slow path on this GlobalState or its descendent. Returns true if it succeeded, false if
    // the slow path was unable to be canceled.
    // Run only from preprocess thread.
    // TODO(jvilk): This responsibility will move to message processing thread.
    bool tryCancelSlowPath(u4 newEpoch);
    // Run only from the typechecking thread.
    // Tries to commit the given epoch. Returns true if the commit succeeeded, or false if it was canceled.
    // The presence of PreemptionTaskManager determines if this commit is preemptible.
    bool tryCommitEpoch(u4 epoch, bool isCancelable,
                        std::optional<std::shared_ptr<PreemptionTaskManager>> preemptionManager,
                        std::function<void()> typecheck);
    // Grabs the epoch lock, and calls function with the current typechecking status.
    void withEpochLock(std::function<void(TypecheckingStatus)> lambda) const;
};

} // namespace sorbet::core::lsp
#endif