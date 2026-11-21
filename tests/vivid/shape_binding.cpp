#include "engine/character/vivid/FaceShapeBinding.h"

#include <cmath>
#include <iostream>
#include <initializer_list>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

using namespace aiverse::vivid;
namespace {
void check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
bool near(float a, float b) { return std::fabs(a-b) < 1e-4f; }
template<typename F> void reject(F&& fn) {
    try { fn(); }
    catch (const std::invalid_argument&) { return; }
    throw std::runtime_error("invalid facial shape mapping accepted");
}
// std::span<const ShapeRoute> cannot be constructed from a braced list in C++20.
// Materialize the routes in a typed container before passing a non-owning view.
void rejectRoutes(const SparseMorphLibrary& library, std::initializer_list<ShapeRoute> routes) {
    const std::vector<ShapeRoute> owned(routes);
    reject([&] { (void)FaceShapeBinding::compile(library, owned); });
}
SparseMorphLibrary fixture() {
    std::vector<MorphTargetInput> targets;
    for (int i=0; i<96; ++i) {
        MorphDelta delta;
        delta.vertex = 0;
        delta.position[0] = 0.01f;
        targets.push_back({"jaw_"+std::to_string(i), {delta}});
    }
    MorphDelta smile;
    smile.vertex=1;
    smile.position[1]=0.5f;
    targets.push_back({"smileLeft", {smile}});
    return SparseMorphLibrary::compile(2, std::move(targets));
}
void validate() {
    const auto library = fixture();
    rejectRoutes(library, {{Channel::Count,"jaw_0",1}});
    rejectRoutes(library, {{Channel::JawOpen,"jaw_1000",1}});
    rejectRoutes(library, {{Channel::JawOpen,"",1}});
    rejectRoutes(library, {{Channel::JawOpen,"jaw_0",-1}});
    rejectRoutes(library, {{Channel::JawOpen,"jaw_0",std::numeric_limits<float>::infinity()}});
    rejectRoutes(library, {{Channel::JawOpen,"jaw_0",1},
                           {Channel::EyeBlinkLeft,"jaw_0",1}});
    check(FaceShapeBinding::compile(library, {}).routes()==0, "empty mapping rejected");
}
void endToEnd() {
    const auto library = fixture();
    std::vector<ShapeRoute> routes;
    for (int i=0; i<96; ++i)
        routes.push_back({Channel::JawOpen, "jaw_"+std::to_string(i), 1.0f});
    routes.push_back({Channel::MouthSmileLeft, "smileLeft", 0.5f});
    const FaceShapeBinding binding = FaceShapeBinding::compile(library, routes);
    check(binding.routes()==97 && binding.targets()==97, "named route count lost");
    FaceController face(0.0f);
    check(face.set(Layer::Tracking, Channel::JawOpen, 0.75f), "tracking setup failed");
    check(face.setEmotion(Emotion::Happy), "emotion setup failed");
    face.snap();
    std::vector<float> weights(library.targets(), -2.0f);
    check(binding.evaluate(face.frame(), weights, 1.0f), "shape weights evaluate failed");
    for (int i=0; i<96; ++i)
        check(near(weights[i], 0.75f), "one-to-many semantic mapping failed");
    check(near(weights.back(), 0.45f), "emotion shape gain failed");

    std::vector<MorphVertex> base(2), output(2);
    base[0].position={0,0,0};
    base[1].position={0,0,0};
    base[0].normal={0,1,0};
    base[1].normal={0,1,0};
    check(library.evaluate(base, weights, output), "CPU reference chain failed");
    check(near(output[0].position[0], 96*0.01f*0.75f), "96 sparse jaw corrections missing");
    check(near(output[1].position[1], 0.5f*0.45f), "smile correction missing");

    FaceFrame invalid = face.frame();
    invalid.values[static_cast<std::size_t>(Channel::JawOpen)] =
        std::numeric_limits<float>::quiet_NaN();
    weights.front() = 0.314f;
    check(!binding.evaluate(invalid, weights), "NaN route input accepted");
    check(near(weights.front(), 0.314f), "error partially wrote destination weights");
    check(!binding.evaluate(face.frame(), weights, -0.1f), "negative influence accepted");
    std::vector<float> tooSmall(2);
    check(!binding.evaluate(face.frame(), tooSmall), "wrong target count accepted");

    check(binding.evaluate(face.frame(), weights, 0.5f), "half blend failed");
    check(near(weights[0], 0.375f), "influence scaling wrong");
    FaceController other(0.0f);
    other.snap();
    std::vector<float> independent(library.targets(), 1.0f);
    check(binding.evaluate(other.frame(), independent), "second instance failed");
    check(near(independent[0], 0.0f), "instance state leaked");
}
} // namespace

int main() {
    try {
        validate();
        endToEnd();
        std::cout << "VIVID shape mapping PASS (97 routes, semantic-to-sparse morph CPU chain)\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "VIVID shape mapping FAIL: " << ex.what() << '\n';
        return 1;
    }
}
