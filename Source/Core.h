#pragma once
#include <algorithm>
#include <cctype>
#include <cmath>
#include <optional>
#include <regex>
#include <string>
#include <vector>

namespace bte
{
inline float dbToGain(float db)
{
    return std::pow(10.0f, db / 20.0f);
}
struct Audio
{
    double rate = 48000;
    std::vector<float> left, right;
};
struct MixState
{
    float db = 0, pan = 0;
    bool mute = false, solo = false;
    double start = 0;
    float trimDb = 0;
    bool mono = false;
    int sidechain = -1;
    int output = -1;
};
// Stateless resampling: timeline position is in seconds, so files of mixed rates stay aligned.
inline float sample(const std::vector<float>& data, double position)
{
    if (position < 0 || position >= static_cast<double>(data.size()))
        return 0;
    auto i = static_cast<size_t>(position);
    auto j = std::min(i + 1, data.size() - 1);
    return data[i] + static_cast<float>(position - static_cast<double>(i)) * (data[j] - data[i]);
}
inline void renderClip(const Audio& audio, double start, double time, double rate, float* left, float* right,
                       int count)
{
    for (int i = 0; i < count; ++i)
    {
        double p = (time + i / rate - start) * audio.rate;
        left[i] = sample(audio.left, p);
        right[i] = sample(audio.right, p);
    }
}
inline void mix(const MixState& s, bool anySolo, const float* left, const float* right, float* outL,
                float* outR, int count)
{
    if (s.mute || (anySolo && !s.solo))
        return;
    // Stereo balance: centre preserves both channels; edges silence the opposite side.
    float gain = dbToGain(s.db);
    float l = gain * (s.mono ? std::sqrt((1.0f - s.pan) * 0.5f) : std::min(1.0f, 1.0f - s.pan));
    float r = gain * (s.mono ? std::sqrt((1.0f + s.pan) * 0.5f) : std::min(1.0f, 1.0f + s.pan));
    for (int i = 0; i < count; ++i)
    {
        outL[i] += left[i] * l;
        outR[i] += right[i] * r;
    }
}
inline void inputGain(const MixState& s, float* l, float* r, int count)
{
    float gain = dbToGain(s.trimDb);
    for (int i = 0; i < count; ++i)
    {
        if (s.mono)
            l[i] = r[i] = (l[i] + r[i]) * 0.5f;
        l[i] *= gain;
        r[i] *= gain;
    }
}
// Whole-clip RMS matching, not perceptual LUFS. Silence is left unchanged.
inline std::optional<float> matchingTrim(const Audio& a, bool mono, float targetDb = -18)
{
    if (a.left.empty())
        return {};
    double sum = 0;
    float peak = 0;
    for (size_t i = 0; i < a.left.size(); ++i)
    {
        float l = a.left[i], r = a.right[i];
        if (mono)
            l = r = (l + r) * 0.5f;
        sum += (static_cast<double>(l) * l + static_cast<double>(r) * r) * 0.5;
        peak = std::max(peak, std::max(std::abs(l), std::abs(r)));
    }
    double rms = std::sqrt(sum / a.left.size());
    if (rms < 1.0e-6 || peak < 1.0e-6f)
        return {};
    float desired = targetDb - 20.0f * std::log10(static_cast<float>(rms));
    float ceiling = -1.0f - 20.0f * std::log10(peak);
    return std::clamp(std::min(desired, ceiling), -60.0f, 24.0f);
}
inline void masterSum(float* l, float* r, int count, float db, bool mono)
{
    float gain = dbToGain(db);
    for (int i = 0; i < count; ++i)
    {
        if (mono)
            l[i] = r[i] = (l[i] + r[i]) * 0.5f;
        l[i] *= gain;
        r[i] *= gain;
    }
}
enum class ActionKind
{
    tempo,
    gainDelta,
    mute,
    solo
};
struct Action
{
    ActionKind kind;
    int track = -1;
    float value = 0;
    std::string summary;
};
// A deliberately bounded offline assistant. It never evaluates code or executes shell commands.
inline std::optional<Action> propose(const std::string& command, int tracks)
{
    std::smatch m;
    if (std::regex_match(
            command, m,
            std::regex(R"(\s*(?:set )?tempo(?: to)? ([0-9]{2,3})(?: bpm)?\s*)", std::regex::icase)))
    {
        float bpm = std::stof(m[1]);
        if (bpm < 30 || bpm > 300)
            return {};
        return Action{ActionKind::tempo, -1, bpm, "Set tempo to " + m[1].str() + " BPM"};
    }
    if (std::regex_match(
            command, m,
            std::regex(R"(\s*(?:turn )?track ([0-9]{1,3}) (up|down) ([0-9]{1,2}(?:\.[0-9])?) db\s*)",
                       std::regex::icase)))
    {
        int index = std::stoi(m[1]) - 1;
        if (index < 0 || index >= tracks)
            return {};
        auto direction = m[2].str();
        std::transform(direction.begin(), direction.end(), direction.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        float value = std::stof(m[3]) * (direction == "down" ? -1.0f : 1.0f);
        return Action{ActionKind::gainDelta, index, value,
                      "Track " + m[1].str() + ": adjust level by " + std::to_string(value) +
                          " dB (limited to -60..+6 dB)"};
    }
    if (std::regex_match(
            command, m,
            std::regex(R"(\s*(mute|unmute|solo|unsolo) track ([0-9]{1,3})\s*)", std::regex::icase)))
    {
        int index = std::stoi(m[2]) - 1;
        if (index < 0 || index >= tracks)
            return {};
        auto verb = m[1].str();
        std::transform(verb.begin(), verb.end(), verb.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        bool solo = verb == "solo" || verb == "unsolo";
        return Action{solo ? ActionKind::solo : ActionKind::mute, index,
                      verb == "mute" || verb == "solo" ? 1.0f : 0.0f, verb + " track " + m[2].str()};
    }
    return {};
}
} // namespace bte
