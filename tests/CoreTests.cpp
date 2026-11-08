#include "Core.h"
#include <iostream>
#include <stdexcept>
using namespace bte;
int checks = 0;
void check(bool value, const char* label)
{
    ++checks;
    if (!value)
        throw std::runtime_error(label);
}
bool near(float a, float b)
{
    return std::abs(a - b) < 0.0001f;
}
int main()
{
    try
    {
        Audio a{4, {0, 1, 0, -1}, {0, 1, 0, -1}};
        float l[8]{}, r[8]{}, ol[8]{}, orr[8]{};
        renderClip(a, 0, 0, 8, l, r, 8);
        check(near(l[1], 0.5f) && near(l[3], 0.5f) && near(l[6], -1), "linear sample rate conversion");
        renderClip(a, 1, 0, 4, l, r, 8);
        check(near(l[3], 0) && near(l[5], 1) && near(l[7], -1), "clip start alignment");
        check(near(sample(a.left, -1), 0) && near(sample(a.left, 4), 0), "clip boundaries");
        MixState m;
        l[0] = 1;
        r[0] = 0.5;
        mix(m, false, l, r, ol, orr, 1);
        check(near(ol[0], 1) && near(orr[0], 0.5), "stereo centre unity");
        m.mute = true;
        mix(m, false, l, r, ol, orr, 1);
        check(near(ol[0], 1), "mute");
        m.mute = false;
        mix(m, true, l, r, ol, orr, 1);
        check(near(ol[0], 1), "non-solo silenced");
        m.solo = true;
        m.pan = -1;
        mix(m, true, l, r, ol, orr, 1);
        check(near(ol[0], 2) && near(orr[0], 0.5), "solo and hard left balance");
        m = {};
        m.mono = true;
        inputGain(m, l, r, 1);
        check(near(l[0], 0.75) && near(r[0], 0.75), "stereo to mono fold");
        ol[0] = orr[0] = 0;
        mix(m, false, l, r, ol, orr, 1);
        check(near(ol[0], 0.75f / std::sqrt(2.0f)), "mono constant power centre");
        m.trimDb = -6;
        l[0] = r[0] = 1;
        inputGain(m, l, r, 1);
        check(near(l[0], dbToGain(-6)), "pre-insert trim");
        Audio constant{48000, std::vector<float>(1000, 0.1f), std::vector<float>(1000, 0.1f)};
        auto matched = matchingTrim(constant, false);
        check(matched && near(*matched, 2), "RMS match target");
        constant.left[0] = constant.right[0] = 1;
        matched = matchingTrim(constant, false);
        check(matched && *matched <= -1, "peak headroom constrains matching");
        Audio silent{48000, std::vector<float>(8, 0), std::vector<float>(8, 0)};
        check(!matchingTrim(silent, false), "silence does not gain boost");
        // Two independent sources accumulate into a bus, then feed a mono master.
        float busL[1]{}, busR[1]{}, masterL[1]{}, masterR[1]{};
        float s1[1]{0.2f}, s2[1]{0.3f};
        m = {};
        mix(m, false, s1, s1, busL, busR, 1);
        mix(m, false, s2, s2, busL, busR, 1);
        m.db = -6;
        mix(m, false, busL, busR, masterL, masterR, 1);
        masterSum(masterL, masterR, 1, 0, true);
        check(near(masterL[0], 0.5f * dbToGain(-6)) && near(masterL[0], masterR[0]),
              "bus sums once with bus fader");
        auto p = propose("set tempo to 128", 3);
        check(p && p->kind == ActionKind::tempo && p->value == 128, "tempo command");
        check(!propose("set tempo to 999", 3), "invalid tempo refused");
        p = propose("Turn track 2 DOWN 3 dB", 3);
        check(p && p->track == 1 && p->value == -3, "gain command");
        p = propose("unsolo track 1", 3);
        check(p && p->kind == ActionKind::solo && p->value == 0, "unsolo command");
        check(!propose("mute track 0", 3) && !propose("mute track 4", 3), "missing tracks refused");
        check(!propose("delete everything", 3) && !propose("mute track 99999999999999999999999", 3),
              "unsupported/oversized input refused");
        std::cout << checks << " checks passed\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
