#pragma once

#include <cstddef>
#include <cstdint>

// Workaround for a lifetime bug between Keen's Havok::HkContactListener and Havok's
// hkpEndOfStepCallbackUtil (SE1-0015).
//
// The listener registers every collision it is told about with the world's end-of-step
// callback util and only unregisters when Havok tells it the collision ended. Havok only
// tells listeners that are still attached to the entity. So when the game detaches a
// listener (ContactPointCallbackEnabled = false, or HkEntity.Dispose) while collisions are
// live, and one of those collisions ends before the next FinishMtStep, the util keeps a
// pointer to a freed contact manager and FinishMtStep calls through it.
//
// The hook runs before HkEntity_SetContactListener(entity, listener, false) and files the
// listener's live registrations into the util's deferred removal list, which FinishMtStep
// processes before it fires anything.
//
// The game gets this fix from the Bugfixes plugin on every platform, so the hook is off by
// default and exists as the reference implementation for the havok_endofstep test. Enable
// it with LNW_HAVOK_ENDOFSTEP_FIX=1, "inplace" to compact the lists in place instead (the
// plugin's algorithm), or "trace" to also log every detach that had registrations to drop.

void havok_endofstep_init(void *image, size_t image_size);
void havok_endofstep_before_set_contact_listener(void *entity, void *listener, bool value);
