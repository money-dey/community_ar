// community_ar_phase2_api.cpp
// =============================================================================
// Phase 2 C ABI implementation. Bridges the public C functions to the
// EffectGraph living inside Phase0Session.
//
// Thread safety: all graph mutations are queued onto the render thread via
// Session::runOnRenderThread(). This keeps the public ABI callable from
// any thread (typically the Flutter platform channel thread) while the
// underlying EffectGraph stays single-threaded.
// =============================================================================

#include "community_ar_phase2_api.h"
#include "../phase0_session.h"
#include "../effects/effect_graph.h"
#include "../effects/effect_types.h"
#include "../effects/effect_base.h"
#include <memory>

namespace community_ar {

// Forward declarations of effect factories. Each factory lives next to its
// effect's logic (e.g. lips_effect.cpp); we just dispatch on type ID here.
std::unique_ptr<Effect> makeLipsEffect(const void* configBytes, size_t configSize);
std::unique_ptr<Effect> makeSkinSmoothEffect(const void* configBytes, size_t configSize);

// -----------------------------------------------------------------------------
// Effect type registry — maps type ID to factory function
// -----------------------------------------------------------------------------
using EffectFactory = std::unique_ptr<Effect>(*)(const void*, size_t);

static EffectFactory factoryForType(uint32_t typeId) {
    switch (typeId) {
        case CAR_EFFECT_LIPS:           return &makeLipsEffect;
        case CAR_EFFECT_SKIN_SMOOTH:    return &makeSkinSmoothEffect;  // Phase 3
        // Phase 6 effects will register here:
        // case CAR_EFFECT_IRIS:        return &makeIrisEffect;
        // case CAR_EFFECT_TEETH:       return &makeTeethEffect;
        // ...
        default:                        return nullptr;
    }
}

}  // namespace community_ar

// -----------------------------------------------------------------------------
// Public ABI
// -----------------------------------------------------------------------------
using namespace community_ar;

extern "C" {

CAR_EXPORT CARStatus car_p2_graph_set(
        CARSession*           session,
        uint32_t              effect_count,
        const uint32_t*       effect_type_ids,
        const void* const*    configs,
        const size_t*         config_sizes) {

    if (!session) return CAR_STATUS_INVALID_SESSION;
    auto* s = reinterpret_cast<Phase0Session*>(session);

    // Build the effect list on the calling thread. The factories themselves
    // do no GPU work, so this is safe off the render thread.
    std::vector<std::unique_ptr<Effect>> built;
    built.reserve(effect_count);

    for (uint32_t i = 0; i < effect_count; ++i) {
        EffectFactory factory = factoryForType(effect_type_ids[i]);
        if (!factory) {
            // Unknown effect type. Bail without partially installing.
            return CAR_STATUS_INVALID_ARGUMENT;
        }

        // COPY the config bytes — the caller may free their buffer right after
        // this function returns, and the factory's call may queue work that
        // outlives the call.
        std::vector<uint8_t> configCopy(
            static_cast<const uint8_t*>(configs[i]),
            static_cast<const uint8_t*>(configs[i]) + config_sizes[i]);

        auto effect = factory(configCopy.data(), configCopy.size());
        if (!effect) return CAR_STATUS_INVALID_ARGUMENT;
        built.push_back(std::move(effect));
    }

    // Hand off to the render thread. The session swaps in the new graph
    // before the next frame's render.
    //
    // The effect list is move-only (vector<unique_ptr<Effect>>), but
    // runOnRenderThread takes a std::function, which must be copyable. Wrap the
    // list in a shared_ptr so the lambda's captures stay copyable; the task
    // moves the vector out of it when it runs.
    auto eff = std::make_shared<std::vector<std::unique_ptr<Effect>>>(
        std::move(built));
    s->runOnRenderThread([s, eff]() {
        s->effectGraph().setEffects(std::move(*eff));
        // After replacing the graph, recompute perception requirements
        s->perceptionPipeline().setRequirements(
            s->effectGraph().perceptionInputs());
    });

    return CAR_STATUS_OK;
}

CAR_EXPORT CARStatus car_p2_graph_clear(CARSession* session) {
    if (!session) return CAR_STATUS_INVALID_SESSION;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    s->runOnRenderThread([s]() {
        s->effectGraph().setEffects({});
        s->perceptionPipeline().setRequirements({});
    });
    return CAR_STATUS_OK;
}

CAR_EXPORT uint32_t car_p2_graph_effect_count(CARSession* session) {
    if (!session) return 0;
    auto* s = reinterpret_cast<Phase0Session*>(session);
    // Read on whatever thread; the count is just an atomic.size() snapshot,
    // OK to read without locks for diagnostics.
    return static_cast<uint32_t>(s->effectGraph().effectCount());
}

}  // extern "C"
