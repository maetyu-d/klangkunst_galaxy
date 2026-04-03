#include "GameComponent.h"
#include <unordered_map>
#include <random>

namespace
{
constexpr float refIsoTileWidth = 12.0f;
constexpr float refIsoTileHeight = 6.0f;
constexpr float refIsoVerticalStep = refIsoTileHeight;

struct ReferenceBackdropTheme
{
    juce::Colour bgTop;
    juce::Colour bgBottom;
    juce::Colour haloLarge;
    juce::Colour haloSmall;
    juce::Colour ring;
    juce::Colour grid;
    juce::Colour headerLine;
};

ReferenceBackdropTheme referenceTheme()
{
    return {
        juce::Colour::fromRGB (26, 10, 28),
        juce::Colour::fromRGB (120, 58, 84),
        juce::Colour::fromRGBA (255, 154, 92, 68),
        juce::Colour::fromRGBA (255, 212, 124, 54),
        juce::Colour::fromRGBA (255, 170, 116, 88),
        juce::Colour::fromRGBA (255, 150, 104, 58),
        juce::Colour::fromRGBA (255, 214, 170, 26)
    };
}

juce::Colour withAlpha (juce::Colour colour, float alpha)
{
    return colour.withAlpha (alpha);
}

void fillGlow (juce::Graphics& g, juce::Rectangle<float> area, juce::Colour colour, float alpha)
{
    juce::ColourGradient glow (withAlpha (colour, alpha), area.getCentreX(), area.getCentreY(),
                               withAlpha (colour, 0.0f), area.getRight(), area.getBottom(), true);
    g.setGradientFill (glow);
    g.fillEllipse (area);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, juce::Colour lightColour)
{
    auto panel = area.toFloat();
    constexpr float radius = 11.0f;
    g.setColour (juce::Colour (0xee070b16));
    g.fillRoundedRectangle (panel, radius);

    juce::ColourGradient wash (juce::Colour::fromRGBA (26, 44, 92, 238), panel.getX(), panel.getY(),
                               juce::Colour::fromRGBA (12, 20, 44, 228), panel.getRight(), panel.getBottom(), false);
    wash.addColour (0.38, juce::Colour::fromRGBA (34, 90, 180, 156));
    wash.addColour (1.0, juce::Colour::fromRGBA (10, 12, 26, 236));
    g.setGradientFill (wash);
    g.fillRoundedRectangle (panel, radius);

    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 34));
    g.fillRoundedRectangle (panel.reduced (3.0f, 3.0f).withHeight (panel.getHeight() * 0.24f), 7.0f);
    g.setColour (withAlpha (lightColour, 0.14f));
    g.fillRoundedRectangle (panel.reduced (3.0f).withTrimmedTop (panel.getHeight() * 0.42f), 8.0f);

    g.setColour (juce::Colour::fromRGBA (118, 236, 255, 174));
    g.drawRoundedRectangle (panel.reduced (1.0f), radius, 1.3f);
    g.setColour (juce::Colour::fromRGBA (22, 40, 78, 255));
    g.drawRoundedRectangle (panel.reduced (4.0f), radius - 3.0f, 1.0f);
}

void drawVignette (juce::Graphics& g, juce::Rectangle<int> bounds)
{
    juce::Path border;
    border.addRectangle (bounds.toFloat());
    border.addRoundedRectangle (bounds.toFloat().reduced (36.0f), 26.0f);
    border.setUsingNonZeroWinding (false);

    g.setColour (juce::Colour (0xcc020202));
    g.fillPath (border);
}

juce::Colour warmInk()
{
    return juce::Colour (0xfff2dfbf);
}

juce::Colour mutedText()
{
    return juce::Colour (0xffbca98b);
}
}

bool GameComponent::WaveVoice::canPlaySound (juce::SynthesiserSound* s)
{
    return dynamic_cast<WaveSound*> (s) != nullptr;
}

void GameComponent::WaveVoice::startNote (int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    const auto sampleRate = getSampleRate();
    level = velocity;
    noteAgeSeconds = 0.0f;
    noiseSeed = static_cast<uint32_t> (0x9E3779B9u ^ (static_cast<uint32_t> (midiNoteNumber) * 2654435761u));
    sampleHoldValue = 0.0f;
    sampleHoldCounter = 0;
    sampleHoldPeriod = 1;
    lpState = 0.0f;
    hpState = 0.0f;
    percussionMode = midiNoteNumber >= 120;
    percussionType = juce::jlimit (0, 3, midiNoteNumber - 120);
    noiseLP = 0.0f;
    noiseHP = 0.0f;
    lastNoise = 0.0f;
    chipSfxType = percussionMode ? 0 : (midiNoteNumber % 4);

    auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz (midiNoteNumber);
    if (engine == SynthEngine::chipPulse)
    {
        cyclesPerSecond *= 4.0;
        level *= 0.70710678f;
    }
    const auto cyclesPerSample = cyclesPerSecond / juce::jmax (1.0, sampleRate);
    angleDelta = cyclesPerSample * juce::MathConstants<double>::twoPi;
    currentAngle = 0.0;
    modAngle = 0.0;
    subAngle = 0.0;

    if (engine == SynthEngine::fmGlass)
        modDelta = cyclesPerSample * 2.0 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::titleBloom)
        modDelta = cyclesPerSample * 1.31 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::digitalV4)
        modDelta = cyclesPerSample * 0.613 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::chipPulse)
        modDelta = cyclesPerSample * 5.37 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::guitarPluck)
        modDelta = cyclesPerSample * 2.01 * juce::MathConstants<double>::twoPi;
    else
        modDelta = cyclesPerSample * 1.997 * juce::MathConstants<double>::twoPi;

    subDelta = cyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;

    if (percussionMode)
    {
        double drumHz = 120.0;
        if (percussionType == 0) drumHz = 52.0;
        else if (percussionType == 1) drumHz = 182.0;
        else if (percussionType == 2) drumHz = 420.0;
        else drumHz = 260.0;

        const double drumCyclesPerSample = drumHz / juce::jmax (1.0, sampleRate);
        angleDelta = drumCyclesPerSample * juce::MathConstants<double>::twoPi;
        modDelta = drumCyclesPerSample * 2.1 * juce::MathConstants<double>::twoPi;
        subDelta = drumCyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;
    }

    ksDelay.clear();
    ksIndex = 0;
    ksLast = 0.0f;
    if (engine == SynthEngine::guitarPluck)
    {
        const double hz = juce::jmax (30.0, cyclesPerSecond);
        const int ksLen = juce::jlimit (16, 4096, static_cast<int> (std::round (sampleRate / hz)));
        ksDelay.resize (static_cast<size_t> (ksLen), 0.0f);
        for (int i = 0; i < ksLen; ++i)
        {
            noiseSeed = noiseSeed * 1664525u + 1013904223u;
            const float n = static_cast<float> ((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;
            ksDelay[static_cast<size_t> (i)] = n * (0.70f * velocity);
        }
    }

    if (percussionMode)
    {
        adsrParams.attack = 0.0005f;
        adsrParams.decay = percussionType == 2 ? 0.035f : percussionType == 0 ? 0.14f : 0.09f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = percussionType == 2 ? 0.01f : 0.03f;
    }
    else if (engine == SynthEngine::digitalV4)
    {
        adsrParams.attack = 0.006f;
        adsrParams.decay = 0.24f;
        adsrParams.sustain = 0.38f;
        adsrParams.release = 0.30f;
    }
    else if (engine == SynthEngine::velvetNoise)
    {
        adsrParams.attack = 0.0004f;
        adsrParams.decay = 0.13f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.025f;
    }
    else if (engine == SynthEngine::titleBloom)
    {
        adsrParams.attack = 0.012f;
        adsrParams.decay = 1.35f;
        adsrParams.sustain = 0.34f;
        adsrParams.release = 0.72f;
    }
    else if (engine == SynthEngine::chipPulse)
    {
        adsrParams.attack = 0.0003f;
        adsrParams.decay = 0.92f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.26f;
    }
    else if (engine == SynthEngine::guitarPluck)
    {
        adsrParams.attack = 0.0004f;
        adsrParams.decay = 0.17f;
        adsrParams.sustain = 0.12f;
        adsrParams.release = 0.14f;
    }
    else
    {
        adsrParams.attack = 0.003f;
        adsrParams.decay = 0.18f;
        adsrParams.sustain = 0.20f;
        adsrParams.release = 0.10f;
    }

    adsr.setParameters (adsrParams);
    adsr.noteOn();
}

void GameComponent::WaveVoice::stopNote (float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
        clearCurrentNote();
}

void GameComponent::WaveVoice::renderNextBlock (juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples)
{
    if (! isVoiceActive())
        return;

    const auto sr = static_cast<float> (juce::jmax (1.0, getSampleRate()));
    for (int s = 0; s < numSamples; ++s)
    {
        const auto env = adsr.getNextSample();
        const auto transient = std::exp (-noteAgeSeconds * 24.0f);

        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const auto white = static_cast<float> ((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;

        if (percussionMode)
        {
            float voicedPerc = 0.0f;
            switch (percussionType)
            {
                case 0:
                {
                    const float pitchDrop = 1.0f + 4.4f * std::exp (-noteAgeSeconds * 48.0f);
                    const float body = static_cast<float> (std::sin (currentAngle * pitchDrop));
                    const float bodyEnv = std::exp (-noteAgeSeconds * 11.5f);
                    const float clickEnv = std::exp (-noteAgeSeconds * 165.0f);
                    const float clickTone = static_cast<float> (std::sin (currentAngle * 10.0));
                    voicedPerc = 0.95f * body * bodyEnv + 0.19f * clickTone * clickEnv + 0.08f * white * clickEnv;
                    break;
                }
                case 1:
                {
                    noiseLP += 0.15f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float snapEnv = std::exp (-noteAgeSeconds * 47.0f);
                    const float toneEnv = std::exp (-noteAgeSeconds * 20.0f);
                    const float tone1 = static_cast<float> (std::sin (currentAngle * 1.86));
                    const float tone2 = static_cast<float> (std::sin (currentAngle * 2.71));
                    const float preSnap = white - lastNoise;
                    voicedPerc = 0.42f * tone1 * toneEnv + 0.18f * tone2 * toneEnv + (0.62f * noiseHP + 0.18f * preSnap) * snapEnv;
                    break;
                }
                case 2:
                {
                    noiseLP += 0.24f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float hat = std::exp (-noteAgeSeconds * 92.0f);
                    const auto metallic = std::copysign (1.0f, white * 0.78f + noiseHP * 0.22f);
                    voicedPerc = (0.56f * metallic + 0.30f * noiseHP) * hat;
                    break;
                }
                case 3:
                default:
                {
                    const auto diff = white - lastNoise;
                    lastNoise = white;
                    const auto clickEnv = static_cast<float> (std::exp (-noteAgeSeconds * 96.0f));
                    const auto gate = (sampleHoldCounter <= 0 ? 1.0f : 0.0f);
                    if (sampleHoldCounter <= 0)
                        sampleHoldCounter = 2 + static_cast<int> (noiseSeed & 3u);
                    --sampleHoldCounter;
                    voicedPerc = (0.74f * diff + 0.16f * white) * clickEnv * gate * 1.6f;
                    break;
                }
            }

            voicedPerc = std::tanh (voicedPerc * 1.75f) * 0.69f;
            const float sample = voicedPerc * level * env * 0.80f;
            for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
                outputBuffer.addSample (c, startSample + s, sample);

            currentAngle += angleDelta;
            modAngle += modDelta;
            subAngle += subDelta;
            if (currentAngle >= juce::MathConstants<double>::twoPi) currentAngle -= juce::MathConstants<double>::twoPi;
            if (modAngle >= juce::MathConstants<double>::twoPi) modAngle -= juce::MathConstants<double>::twoPi;
            if (subAngle >= juce::MathConstants<double>::twoPi) subAngle -= juce::MathConstants<double>::twoPi;
            noteAgeSeconds += 1.0f / sr;
            continue;
        }

        const auto sub = static_cast<float> (std::sin (subAngle));
        const auto click = white * transient * 0.18f;
        float voiced = 0.0f;

        if (engine == SynthEngine::digitalV4)
        {
            const auto vibrato = 0.009 * std::sin (modAngle * 0.14);
            const auto oscA = std::sin (currentAngle + vibrato);
            const auto oscB = std::sin (currentAngle * 2.0 + 0.12 * std::sin (modAngle * 0.10));
            const auto oscC = std::sin (currentAngle * 3.0 + 0.06 * std::sin (modAngle * 0.06));
            const auto subWarm = std::sin (subAngle + 0.05 * std::sin (modAngle * 0.05));
            const auto raw = static_cast<float> (0.67 * oscA + 0.14 * oscB + 0.06 * oscC + 0.16 * subWarm + click * 0.002f);
            const auto cutoff = 180.0f + 1020.0f * env + 130.0f * static_cast<float> (0.5 + 0.5 * std::sin (modAngle * 0.03));
            const auto alpha = std::exp (-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const auto hp = raw - lpState;
            hpState = 0.996f * hpState + 0.004f * hp;
            voiced = static_cast<float> (std::tanh ((0.97f * lpState + 0.005f * hpState + 0.06f * subWarm) * 0.90f) * 0.66f);
        }
        else if (engine == SynthEngine::fmGlass)
        {
            const float idxMain = 0.72f * std::exp (-noteAgeSeconds * 3.6f) + 0.13f;
            const float idxAir = 0.21f * std::exp (-noteAgeSeconds * 6.8f);
            const auto slowDrift = 0.025f * std::sin (modAngle * 0.11);
            const auto mod1 = std::sin (modAngle + slowDrift);
            const auto mod2 = std::sin (modAngle * 0.75 + 0.6 * std::sin (modAngle * 0.09));
            const auto carrier = std::sin (currentAngle + idxMain * mod1 + idxAir * mod2);
            const auto harmonic2 = std::sin (currentAngle * 2.0 + idxMain * 0.22f * mod1);
            const auto harmonic3 = std::sin (currentAngle * 3.0 + idxMain * 0.10f * mod2);
            const auto glass = std::sin (currentAngle * 4.0 + modAngle * 0.07);
            const auto raw = static_cast<float> (0.81 * carrier + 0.11 * harmonic2 + 0.05 * harmonic3 + 0.03 * glass + click * 0.012f);
            const auto cutoff = 520.0f + 2350.0f * env + 240.0f * static_cast<float> (0.5 + 0.5 * std::sin (modAngle * 0.02));
            const auto alpha = std::exp (-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const auto hp = raw - lpState;
            hpState = 0.993f * hpState + 0.007f * hp;
            voiced = static_cast<float> (std::tanh ((0.95f * lpState + 0.04f * hpState) * 0.96f) * 0.67f);
        }
        else if (engine == SynthEngine::titleBloom)
        {
            const float bloomEnv = std::exp (-noteAgeSeconds * 1.7f);
            const float shimmerEnv = std::exp (-noteAgeSeconds * 5.4f);
            const float airEnv = std::exp (-noteAgeSeconds * 10.0f);
            const auto modA = std::sin (modAngle);
            const auto modB = std::sin (modAngle * 0.53 + 0.7 * std::sin (modAngle * 0.11));
            const auto carrier = std::sin (currentAngle + 0.34f * modA + 0.09f * modB);
            const auto halo = std::sin (currentAngle * 2.0 + 0.11f * modA);
            const auto shimmer = std::sin (currentAngle * 3.0 + 0.07f * modB);
            const auto air = std::sin (currentAngle * 4.97 + modAngle * 0.08);
            const float raw = static_cast<float> (0.70 * carrier
                                                  + 0.16 * halo * bloomEnv
                                                  + 0.09 * shimmer * shimmerEnv
                                                  + 0.05 * air * airEnv
                                                  + 0.08 * sub * bloomEnv
                                                  + click * 0.010f);
            const float cutoff = 420.0f + 1850.0f * env + 320.0f * static_cast<float> (0.5 + 0.5 * std::sin (modAngle * 0.03));
            const float alpha = std::exp (-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const float hp = raw - lpState;
            hpState = 0.996f * hpState + 0.004f * hp;
            voiced = static_cast<float> (std::tanh ((0.90f * lpState + 0.10f * hpState + 0.05f * raw) * 0.92f) * 0.62f);
        }
        else if (engine == SynthEngine::chipPulse)
        {
            const float toneEnv = std::exp (-noteAgeSeconds * 4.8f);
            const float metalEnv = std::exp (-noteAgeSeconds * 7.8f);
            const float sparkleEnv = std::exp (-noteAgeSeconds * 16.0f);
            const float strikeEnv = std::exp (-noteAgeSeconds * 84.0f);
            noiseLP += 0.22f * (white - noiseLP);

            const auto fundamental = std::sin (currentAngle);
            const auto bell2 = std::sin (currentAngle * 2.76 + 0.18 * std::sin (modAngle * 0.27));
            const auto bell3 = std::sin (currentAngle * 5.41 + 0.13 * std::sin (modAngle * 0.38));
            const auto bell4 = std::sin (currentAngle * 8.93 + 0.09 * std::sin (modAngle * 0.51));
            const auto air = std::sin (currentAngle * 11.8 + modAngle * 0.15);
            const float strike = (0.64f * white + 0.36f * (white - noiseLP)) * strikeEnv * 0.24f;

            const float raw = static_cast<float> (0.58 * fundamental * toneEnv
                                                  + 0.24 * bell2 * metalEnv
                                                  + 0.12 * bell3 * metalEnv
                                                  + 0.07 * bell4 * sparkleEnv
                                                  + 0.04 * air * sparkleEnv
                                                  + 0.05 * sub * toneEnv
                                                  + strike
                                                  + click * 0.018f);

            const float cutoff = 900.0f + 3400.0f * std::exp (-noteAgeSeconds * 2.2f);
            const float alpha = std::exp (-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const float hp = raw - lpState;
            hpState = 0.989f * hpState + 0.011f * hp;
            voiced = std::tanh ((0.78f * lpState + 0.28f * hpState + 0.08f * raw) * 1.26f) * 0.70f;
        }
        else if (engine == SynthEngine::guitarPluck)
        {
            if (! ksDelay.empty())
            {
                const int n = static_cast<int> (ksDelay.size());
                const int nextIdx = (ksIndex + 1) % n;
                const float y0 = ksDelay[static_cast<size_t> (ksIndex)];
                const float y1 = ksDelay[static_cast<size_t> (nextIdx)];
                const float avg = 0.5f * (y0 + y1);
                const float damping = 0.9925f - 0.012f * static_cast<float> (0.5 + 0.5 * std::sin (modAngle * 0.04));
                const float pickBurst = (white - noiseLP) * std::exp (-noteAgeSeconds * 62.0f) * 0.040f;
                noiseLP += 0.25f * (white - noiseLP);
                const float write = avg * damping + pickBurst;
                ksDelay[static_cast<size_t> (ksIndex)] = write;
                ksIndex = nextIdx;
                const float body = 0.82f * y0 + 0.12f * sub + 0.08f * click;
                lpState += 0.14f * (body - lpState);
                hpState += 0.010f * ((body - lpState) - hpState);
                voiced = static_cast<float> (std::tanh ((0.92f * lpState + 0.05f * hpState + 0.08f * ksLast) * 1.12f) * 0.72f);
                ksLast = y0;
            }
        }
        else
        {
            const float toneEnv = std::exp (-noteAgeSeconds * 9.5f);
            const float metalEnv = std::exp (-noteAgeSeconds * 17.0f);
            const float strikeEnv = std::exp (-noteAgeSeconds * 95.0f);
            noiseLP += 0.36f * (white - noiseLP);
            const float strike = (0.72f * white + 0.28f * (white - noiseLP)) * strikeEnv * 0.20f;
            const auto fundamental = std::sin (currentAngle);
            const auto r2 = std::sin (currentAngle * 3.99 + 0.12 * std::sin (modAngle * 0.3));
            const auto r3 = std::sin (currentAngle * 6.83 + 0.08 * std::sin (modAngle * 0.43));
            const auto r4 = std::sin (currentAngle * 9.77 + 0.05 * std::sin (modAngle * 0.57));
            const auto body = static_cast<float> (0.74 * fundamental * toneEnv + 0.17 * r2 * metalEnv
                                                  + 0.07 * r3 * metalEnv + 0.04 * r4 * metalEnv + 0.06 * sub * toneEnv);
            lpState += 0.18f * ((body + strike) - lpState);
            voiced = std::tanh ((0.82f * lpState + 0.18f * body + strike) * 1.34f) * 0.72f;
        }

        const float sample = voiced * level * env;
        for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
            outputBuffer.addSample (c, startSample + s, sample);

        currentAngle += angleDelta;
        modAngle += modDelta;
        subAngle += subDelta;
        if (currentAngle >= juce::MathConstants<double>::twoPi) currentAngle -= juce::MathConstants<double>::twoPi;
        if (modAngle >= juce::MathConstants<double>::twoPi) modAngle -= juce::MathConstants<double>::twoPi;
        if (subAngle >= juce::MathConstants<double>::twoPi) subAngle -= juce::MathConstants<double>::twoPi;
        noteAgeSeconds += 1.0f / sr;
    }

    if (! adsr.isActive())
        clearCurrentNote();
}

GameComponent::GameComponent()
    : galaxy (GalaxyGenerator::generateGalaxy (0x4B4C414E))
{
    for (int i = 0; i < 10; ++i)
        synth.addVoice (new WaveVoice (synthEngine));
    synth.addSound (new WaveSound());

    for (int i = 0; i < 10; ++i)
        beatSynth.addVoice (new WaveVoice (synthEngine));
    beatSynth.addSound (new WaveSound());

    setOpaque (true);
    setWantsKeyboardFocus (true);
    setAudioChannels (0, 2);
    grabKeyboardFocus();
    updateMusicState();
    startTimerHz (30);
}

GameComponent::~GameComponent()
{
    saveActivePlanet();
    shutdownAudio();
}

void GameComponent::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    const auto theme = referenceTheme();
    const float pulse = 0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0013));
    const float beatPulse = 0.5f + 0.5f * std::sin (transportPhase * juce::MathConstants<float>::twoPi);
    const float barPulse = 0.5f + 0.5f * std::sin (transportPhase * juce::MathConstants<float>::twoPi * 0.25f);
    juce::ColourGradient bg (theme.bgTop,
                             static_cast<float> (bounds.getCentreX()), static_cast<float> (bounds.getY()),
                             theme.bgBottom,
                             static_cast<float> (bounds.getCentreX()), static_cast<float> (bounds.getBottom()),
                             false);
    g.setGradientFill (bg);
    g.fillAll();

    auto floatBounds = bounds.toFloat();
    g.setColour (theme.haloLarge.withAlpha (static_cast<float> (34 + 18 * pulse) / 255.0f));
    g.fillEllipse (floatBounds.withSizeKeepingCentre (floatBounds.getWidth() * 0.82f, floatBounds.getHeight() * 0.56f)
                       .translated (0.0f, floatBounds.getHeight() * 0.08f));
    g.setColour (theme.haloSmall.withAlpha (static_cast<float> (22 + 18 * pulse) / 255.0f));
    g.fillEllipse (floatBounds.withSizeKeepingCentre (floatBounds.getWidth() * 0.46f, floatBounds.getHeight() * 0.30f)
                       .translated (floatBounds.getWidth() * 0.10f, -floatBounds.getHeight() * 0.08f));

    const auto centre = floatBounds.getCentre();
    juce::Path rings;
    for (int i = 0; i < 6; ++i)
    {
        const float w = floatBounds.getWidth() * (0.20f + i * 0.12f);
        const float h = floatBounds.getHeight() * (0.12f + i * 0.08f);
        rings.addEllipse (juce::Rectangle<float> (w, h).withCentre ({ centre.x, centre.y + floatBounds.getHeight() * 0.18f }));
    }
    g.setColour (theme.ring.withAlpha (static_cast<float> (14 + 10 * pulse) / 255.0f));
    g.strokePath (rings, juce::PathStrokeType (1.2f));

    juce::Path grid;
    constexpr int cols = 10;
    constexpr int rows = 7;
    for (int i = 0; i <= cols; ++i)
    {
        const float x = floatBounds.getX() + floatBounds.getWidth() * (static_cast<float> (i) / static_cast<float> (cols));
        grid.startNewSubPath (x, floatBounds.getCentreY() - 20.0f);
        grid.lineTo (centre.x + (x - centre.x) * 0.18f, floatBounds.getBottom());
    }
    for (int j = 0; j <= rows; ++j)
    {
        const float t = static_cast<float> (j) / static_cast<float> (rows);
        const float y = floatBounds.getCentreY() + t * t * floatBounds.getHeight() * 0.42f;
        grid.startNewSubPath (floatBounds.getX(), y);
        grid.lineTo (floatBounds.getRight(), y);
    }
    g.setColour (theme.grid);
    g.strokePath (grid, juce::PathStrokeType (1.0f));

    g.setColour (theme.headerLine.withAlpha (static_cast<float> (8 + 8 * pulse) / 255.0f));
    g.drawLine (floatBounds.getX() + 32.0f, floatBounds.getY() + 112.0f,
                floatBounds.getRight() - 32.0f, floatBounds.getY() + 112.0f, 1.0f);

    juce::Path beatRings;
    for (int i = 0; i < 2; ++i)
    {
        const float expand = 1.0f + beatPulse * (0.12f + 0.06f * static_cast<float> (i));
        const float w = floatBounds.getWidth() * (0.34f + 0.16f * static_cast<float> (i)) * expand;
        const float h = floatBounds.getHeight() * (0.16f + 0.08f * static_cast<float> (i)) * expand;
        beatRings.addEllipse (juce::Rectangle<float> (w, h).withCentre ({ centre.x, centre.y + floatBounds.getHeight() * 0.18f }));
    }
    g.setColour (theme.ring.withAlpha (static_cast<float> (8 + 26 * beatPulse + 16 * barPulse) / 255.0f));
    g.strokePath (beatRings, juce::PathStrokeType (1.0f + 1.2f * beatPulse));

    for (int i = 0; i < 24; ++i)
    {
        const float t = static_cast<float> (i) / 24.0f;
        const float x = floatBounds.getX() + floatBounds.getWidth() * std::fmod (0.11f * i + 0.02f * pulse, 1.0f);
        const float y = floatBounds.getY() + floatBounds.getHeight() * (0.08f + t * 0.72f);
        const float size = 1.4f + 1.8f * std::fmod (t * 7.0f + pulse, 1.0f);
        g.setColour (juce::Colour::fromRGBA (190, 230, 255, static_cast<uint8_t> (16 + 22 * std::fmod (t * 9.0f + pulse, 1.0f))));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ x, y }));
    }

    if (currentScene != Scene::title)
        drawHeader (g, bounds.removeFromTop (70));

    auto content = bounds.reduced (30);
    switch (currentScene)
    {
        case Scene::title:   drawTitleScene (g, content); break;
        case Scene::galaxy:  drawGalaxyScene (g, content); break;
        case Scene::landing: drawLandingScene (g, content); break;
        case Scene::builder: drawBuilderScene (g, content); break;
    }

    drawVignette (g, getLocalBounds());
}

void GameComponent::resized()
{
    if (firstPersonCursorCaptured)
        recenterFirstPersonMouse();
}

void GameComponent::prepareToPlay (int, double sampleRate)
{
    const juce::ScopedLock sl (synthLock);
    currentSampleRate = sampleRate;
    synth.setCurrentPlaybackSampleRate (sampleRate);
    beatSynth.setCurrentPlaybackSampleRate (sampleRate);
}

void GameComponent::getNextAudioBlock (const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    if (currentScene == Scene::builder && performanceMode)
    {
        const juce::ScopedLock sl (synthLock);
        juce::MidiBuffer midi;
        juce::MidiBuffer beatMidi;
        const float blockSeconds = static_cast<float> (bufferToFill.numSamples / juce::jmax (1.0, currentSampleRate));
        const double stepSeconds = 60.0 / juce::jmax (60.0, performanceBpm) / 4.0;

        double localTime = 0.0;
        while (localTime < blockSeconds)
        {
            const double remainingToStep = stepSeconds - beatStepAccumulator;
            if (localTime + remainingToStep > blockSeconds)
            {
                beatStepAccumulator += blockSeconds - localTime;
                break;
            }

            localTime += remainingToStep;
            beatStepAccumulator = 0.0;
            const int step = beatStepIndex % 16;
            const int phrase = beatBarIndex % 4;
            const int sampleOffset = juce::jlimit (0, juce::jmax (0, bufferToFill.numSamples - 1),
                                                   static_cast<int> (std::round (localTime * currentSampleRate)));
            const float density = juce::jlimit (0.0f, 1.0f,
                                                performanceBeatEnergy
                                                    + 0.04f * static_cast<float> (performanceSnakes.size())
                                                    + 0.02f * static_cast<float> (performanceDiscs.size()));

            if (drumMode == DrumMode::reactiveBreakbeat)
            {
                const bool dense = density > 0.45f;
                const bool frantic = density > 0.72f;
                const bool kick = step == 0
                               || step == 10
                               || (step == 7 && phrase >= 1)
                               || (step == 13 && phrase == 3)
                               || (step == 3 && phrase == 2)
                               || (dense && (step == 6 || step == 14))
                               || (frantic && (step == 2 || step == 11));
                const bool snare = step == 4 || step == 12;
                const bool ghostSnare = (step == 3 && phrase != 0)
                                     || (step == 11 && phrase >= 2)
                                     || (step == 15 && phrase == 3)
                                     || (dense && step == 6)
                                     || (frantic && (step == 9 || step == 14));
                const bool hat = step != 4 && step != 12;
                const bool openHat = step == 6 || step == 14 || (dense && step == 11);
                const bool glitch = (step == 9 && phrase >= 2) || (step == 15 && phrase == 1) || (frantic && (step == 5 || step == 13));

                if (kick) addBeatEvent (beatMidi, 120, step == 0 ? 0.82f : 0.60f, sampleOffset, bufferToFill.numSamples);
                if (snare) addBeatEvent (beatMidi, 121, 0.62f + 0.06f * static_cast<float> (phrase == 3), sampleOffset, bufferToFill.numSamples);
                if (ghostSnare) addBeatEvent (beatMidi, 121, 0.20f + 0.05f * static_cast<float> (phrase), sampleOffset + bufferToFill.numSamples / 32, bufferToFill.numSamples);
                if (hat) addBeatEvent (beatMidi, 122, (step % 2 == 0) ? 0.30f : 0.18f, sampleOffset, bufferToFill.numSamples);
                if (openHat) addBeatEvent (beatMidi, 122, 0.22f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                if ((step == 5 || step == 13) && (phrase >= 1 || dense))
                {
                    addBeatEvent (beatMidi, 122, 0.16f, sampleOffset + bufferToFill.numSamples / 48, bufferToFill.numSamples);
                    addBeatEvent (beatMidi, 122, 0.14f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                }
                if (dense && (step == 7 || step == 15))
                {
                    addBeatEvent (beatMidi, 122, 0.12f, sampleOffset + bufferToFill.numSamples / 64, bufferToFill.numSamples);
                    addBeatEvent (beatMidi, 122, 0.11f, sampleOffset + bufferToFill.numSamples / 40, bufferToFill.numSamples);
                    if (frantic)
                        addBeatEvent (beatMidi, 122, 0.10f, sampleOffset + bufferToFill.numSamples / 24, bufferToFill.numSamples);
                }
                if (glitch) addBeatEvent (beatMidi, 123, 0.24f + 0.08f * static_cast<float> (phrase), sampleOffset, bufferToFill.numSamples);
            }
            else
            {
                auto hit = [this, &beatMidi, sampleOffset, &bufferToFill] (int midiNote, float velocity)
                {
                    addBeatEvent (beatMidi, midiNote, juce::jlimit (0.0f, 1.0f, velocity * 0.80f), sampleOffset, bufferToFill.numSamples);
                };

                switch (drumMode)
                {
                    case DrumMode::rezStraight:
                        if ((step % 4) == 0) hit (120, 0.82f);
                        if (step == 4 || step == 12) hit (121, 0.60f);
                        if ((step % 2) == 1) hit (122, 0.18f + ((step % 4) == 3 ? 0.05f : 0.0f));
                        break;
                    case DrumMode::tightPulse:
                        if (step == 0 || step == 8 || step == 12) hit (120, 0.78f);
                        if (step == 4 || step == 12) hit (121, 0.58f);
                        if ((step % 2) == 1) hit (122, 0.17f);
                        if (step == 15) hit (123, 0.18f);
                        break;
                    case DrumMode::forwardStep:
                        if (step == 0 || step == 8 || step == 14) hit (120, 0.76f);
                        if (step == 4 || step == 12) hit (121, 0.56f);
                        if ((step % 2) == 1) hit (122, 0.16f + ((step == 11 || step == 15) ? 0.05f : 0.0f));
                        break;
                    case DrumMode::railLine:
                        if ((step % 4) == 0) hit (120, 0.72f);
                        if (step == 4 || step == 12) hit (121, 0.52f);
                        if ((step % 2) == 1) hit (122, 0.15f);
                        if (step == 8 || step == 15) hit (123, 0.16f);
                        break;
                    case DrumMode::reactiveBreakbeat:
                    default:
                        break;
                }
            }

            ++beatStepIndex;
            if (beatStepIndex % 16 == 0)
                ++beatBarIndex;
        }

        for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
        {
            if (it->secondsRemaining <= blockSeconds)
            {
                midi.addEvent (juce::MidiMessage::noteOff (1, it->note), juce::jmax (0, bufferToFill.numSamples - 1));
                it = pendingNoteOffs.erase (it);
            }
            else
            {
                it->secondsRemaining -= blockSeconds;
                ++it;
            }
        }

        for (auto it = pendingBeatNoteOffs.begin(); it != pendingBeatNoteOffs.end();)
        {
            if (it->secondsRemaining <= blockSeconds)
            {
                beatMidi.addEvent (juce::MidiMessage::noteOff (1, it->note), juce::jmax (0, bufferToFill.numSamples - 1));
                it = pendingBeatNoteOffs.erase (it);
            }
            else
            {
                it->secondsRemaining -= blockSeconds;
                ++it;
            }
        }

        transportPhase += static_cast<float> ((performanceBpm / 60.0) * blockSeconds * 0.25);
        while (transportPhase > 1.0f)
            transportPhase -= 1.0f;

        synth.renderNextBlock (*bufferToFill.buffer, midi, bufferToFill.startSample, bufferToFill.numSamples);
        if (! performanceBeatMuted)
            beatSynth.renderNextBlock (*bufferToFill.buffer, beatMidi, bufferToFill.startSample, bufferToFill.numSamples);
        return;
    }

    const juce::ScopedLock sl (synthLock);
    juce::MidiBuffer midi;
    const float blockSeconds = static_cast<float> (bufferToFill.numSamples / juce::jmax (1.0, currentSampleRate));
    const double ambientBpm = static_cast<double> (juce::jmap (transportRate, 0.16f, 0.42f, 72.0f, 128.0f));
    const double stepSeconds = 60.0 / ambientBpm / (currentScene == Scene::title ? 1.0 : 2.0);
    const auto chordNotes = getAmbientChordMidiNotes();

    auto addAmbientNote = [this, &midi, &bufferToFill] (int midiNote, float velocity, float lengthSeconds, int sampleOffset)
    {
        midi.addEvent (juce::MidiMessage::noteOn (1, midiNote, juce::jlimit (0.0f, 1.0f, velocity)), sampleOffset);
        schedulePendingNoteOff (pendingNoteOffs, midiNote, lengthSeconds);
    };

    double localTime = 0.0;
    while (localTime < blockSeconds)
    {
        const double remainingToStep = stepSeconds - ambientStepAccumulator;
        if (localTime + remainingToStep > blockSeconds)
        {
            ambientStepAccumulator += blockSeconds - localTime;
            break;
        }

        localTime += remainingToStep;
        ambientStepAccumulator = 0.0;
        const int sampleOffset = juce::jlimit (0, juce::jmax (0, bufferToFill.numSamples - 1),
                                               static_cast<int> (std::round (localTime * currentSampleRate)));
        const int phraseStep = ambientStepIndex % 8;

        if (currentScene == Scene::title)
        {
            for (size_t i = 0; i < chordNotes.size(); ++i)
                addAmbientNote (chordNotes[i], 0.10f + 0.025f * static_cast<float> (i), 0.95f, sampleOffset);

            if ((phraseStep % 2) == 0)
                addAmbientNote (quantizePerformanceMidi (getAmbientRootMidi() + 12 + ((phraseStep / 2) % 3) * 2), 0.12f, 0.28f, sampleOffset);
        }
        else if (currentScene == Scene::galaxy)
        {
            for (size_t i = 0; i < chordNotes.size(); ++i)
                addAmbientNote (chordNotes[i], 0.11f + 0.03f * static_cast<float> (i == 0), 0.78f, sampleOffset);

            if ((phraseStep % 2) == 1)
                addAmbientNote (quantizePerformanceMidi (getAmbientRootMidi() + 7 + ((ambientStepIndex / 2) % 4)), 0.10f, 0.18f, sampleOffset);
        }
        else if (currentScene == Scene::landing)
        {
            for (size_t i = 0; i < chordNotes.size(); ++i)
                addAmbientNote (chordNotes[i], 0.12f + 0.02f * static_cast<float> (i), 0.62f, sampleOffset);

            addAmbientNote (quantizePerformanceMidi (getAmbientRootMidi() + 12 + (phraseStep % 3) * 2), 0.11f, 0.20f, sampleOffset);
        }
        else
        {
            for (size_t i = 0; i < chordNotes.size(); ++i)
                addAmbientNote (chordNotes[i], 0.10f + 0.02f * static_cast<float> (i), 0.36f, sampleOffset);

            if ((phraseStep % 2) == 0)
                addAmbientNote (quantizePerformanceMidi (getAmbientRootMidi() + 12 + (phraseStep % 4)), 0.09f, 0.14f, sampleOffset);
        }

        ++ambientStepIndex;
    }

    for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
    {
        if (it->secondsRemaining <= blockSeconds)
        {
            midi.addEvent (juce::MidiMessage::noteOff (1, it->note), juce::jmax (0, bufferToFill.numSamples - 1));
            it = pendingNoteOffs.erase (it);
        }
        else
        {
            it->secondsRemaining -= blockSeconds;
            ++it;
        }
    }

    transportPhase += static_cast<float> ((ambientBpm / 60.0) * blockSeconds * 0.25);
    while (transportPhase > 1.0f)
        transportPhase -= 1.0f;

    synth.renderNextBlock (*bufferToFill.buffer, midi, bufferToFill.startSample, bufferToFill.numSamples);
}

void GameComponent::releaseResources()
{
    const juce::ScopedLock sl (synthLock);
    pendingNoteOffs.clear();
    pendingBeatNoteOffs.clear();
}

void GameComponent::mouseMove (const juce::MouseEvent& event)
{
    if (currentScene == Scene::title)
    {
        const auto nextAction = titleSlotOverlayMode == TitleSlotOverlayMode::none
                                    ? titleActionAt (event.position, getTitleInteractionArea())
                                    : TitleAction::none;
        if (nextAction != hoveredTitleAction)
        {
            hoveredTitleAction = nextAction;
            repaint();
        }
        return;
    }

    if (currentScene != Scene::builder)
        return;

    if (performanceMode)
    {
        if (const auto cell = getPerformanceCellAtPosition (event.position, getBuilderGridArea().toFloat()))
        {
            if (! performanceHoverCell.has_value() || *performanceHoverCell != *cell)
            {
                performanceHoverCell = *cell;
                repaint();
            }
        }
        else if (performanceHoverCell.has_value())
        {
            performanceHoverCell.reset();
            repaint();
        }
        return;
    }

    if (topDownBuildMode == TopDownBuildMode::tetris || topDownBuildMode == TopDownBuildMode::cellularAutomata)
    {
        if (const auto cell = getPerformanceCellAtPosition (event.position, getBuilderGridArea().toFloat()))
        {
            if (topDownBuildMode == TopDownBuildMode::tetris)
            {
                if (! tetrisPiece.active)
                    spawnTetrisPiece (false);

                auto moved = tetrisPiece;
                moved.anchor.x = cell->x;
                clampTetrisPieceToSurface (moved);

                if (tetrisPieceFits (moved) && ! tetrisPieceCollidesWithVoxels (moved))
                    tetrisPiece = moved;
            }
            else
            {
                automataHoverCell = *cell;
            }
            repaint();
        }
        return;
    }

    if (builderViewMode == BuilderViewMode::firstPerson)
    {
        if (! firstPersonCursorCaptured)
            return;

        const auto centre = getLocalBounds().getCentre().toFloat();

        if (suppressNextMouseMove)
        {
            suppressNextMouseMove = false;
            lastMousePosition = centre;
            return;
        }

        const auto delta = event.position - centre;
        if (std::abs (delta.x) < 0.01f && std::abs (delta.y) < 0.01f)
            return;

        constexpr float mouseSensitivity = 0.0027f;
        firstPersonState.yaw += delta.x * mouseSensitivity;
        firstPersonState.pitch = juce::jlimit (-1.45f, 1.45f, firstPersonState.pitch - delta.y * mouseSensitivity);
        syncCursorToFirstPersonTarget();
        repaint();

        suppressNextMouseMove = true;
        recenterFirstPersonMouse();
        return;
    }

    if (updateIsometricCursorFromPosition (event.position))
        repaint();
}

void GameComponent::mouseDrag (const juce::MouseEvent& event)
{
    mouseMove (event);
}

void GameComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
{
    if (currentScene == Scene::galaxy && galaxyLogOpen)
    {
        const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
        const float scrollAmount = delta * (wheel.isSmooth ? 96.0f : 148.0f);
        galaxyLogScroll = juce::jlimit (0.0f, getGalaxyLogMaxScroll (getLocalBounds()), galaxyLogScroll - scrollAmount);
        repaint();
        return;
    }

    if (currentScene != Scene::builder)
        return;

    if (performanceMode)
        return;

    if (topDownBuildMode != TopDownBuildMode::none)
        return;

    const float delta = wheel.deltaY != 0.0f ? wheel.deltaY : wheel.deltaX;
    const float sensitivity = wheel.isSmooth ? 0.30f : 0.48f;
    const float factor = std::exp (delta * sensitivity);
    isometricCamera.zoom = juce::jlimit (0.2f, 10.0f, isometricCamera.zoom * factor);
    repaint();
}

void GameComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! hasKeyboardFocus (true))
        grabKeyboardFocus();

    if (currentScene == Scene::builder && performanceMode)
        return;

    if (currentScene == Scene::builder && builderViewMode == BuilderViewMode::firstPerson)
    {
        if (event.mods.isLeftButtonDown())
            applyFirstPersonAction (true);

        if (event.mods.isRightButtonDown())
            applyFirstPersonAction (false);

        return;
    }

    lastMousePosition = event.position;
    hasMouseAnchor = true;
}

void GameComponent::mouseUp (const juce::MouseEvent& event)
{
    if (currentScene == Scene::title)
    {
        const auto titleArea = getTitleInteractionArea();
        if (titleSlotOverlayMode != TitleSlotOverlayMode::none)
        {
            const auto overlay = getTitleSlotOverlayBounds (titleArea);
            if (titleSlotOverlayMode == TitleSlotOverlayMode::save
                && getTitleSlotInputBounds (overlay).contains (event.position))
            {
                titleSelectedSlotIndex = -1;
                titleSlotStatusMessage = "Type a slot name, then click SAVE .DRD.";
                repaint();
                return;
            }

            if (titleSlotOverlayMode == TitleSlotOverlayMode::save
                && getTitleSlotRenameBounds (overlay).contains (event.position)
                && titleSelectedSlotIndex >= 0)
            {
                performTitleRenameSlot (titleSelectedSlotIndex, titleSlotNameDraft);
                return;
            }

            if (getTitleSlotDeleteBounds (overlay).contains (event.position)
                && titleSelectedSlotIndex >= 0)
            {
                performTitleDeleteSlot (titleSelectedSlotIndex);
                return;
            }

            if (getTitleSlotConfirmBounds (overlay).contains (event.position))
            {
                if (titleSlotOverlayMode == TitleSlotOverlayMode::save)
                    performTitleSaveToSlot (titleSlotNameDraft);
                else if (titleSelectedSlotIndex >= 0)
                    performTitleLoadFromSlot (titleSelectedSlotIndex);
                return;
            }

            for (int i = 0; i < static_cast<int> (titleSaveSlots.size()); ++i)
            {
                if (! getTitleSlotRowBounds (overlay, i).contains (event.position))
                    continue;

                titleSelectedSlotIndex = i;
                titleSlotNameDraft = titleSaveSlots[static_cast<size_t> (i)].slotName;

                repaint();
                return;
            }

            if (! overlay.contains (event.position))
            {
                closeTitleSlotOverlay();
                repaint();
            }

            return;
        }

        switch (titleActionAt (event.position, titleArea))
        {
            case TitleAction::resumeVoyage:     if (isTitleActionEnabled (TitleAction::resumeVoyage)) setScene (resumeScene); break;
            case TitleAction::loadVoyage:       openTitleSlotOverlay (TitleSlotOverlayMode::load); break;
            case TitleAction::newVoyage:        enterGalaxyFromTitle (true); break;
            case TitleAction::saveVoyage:       openTitleSlotOverlay (TitleSlotOverlayMode::save); break;
            case TitleAction::none: break;
        }
        return;
    }

    if (currentScene == Scene::galaxy && galaxyLogOpen)
    {
        if (! expandedLogHeatmapPlanetId.isEmpty())
        {
            expandedLogHeatmapPlanetId.clear();
            repaint();
            return;
        }

        for (const auto& hit : logHeatmapHitTargets)
        {
            if (hit.bounds.contains (event.position))
            {
                expandedLogHeatmapPlanetId = hit.planetId;
                repaint();
                return;
            }
        }
    }

    if (currentScene == Scene::builder)
    {
        if (performanceMode)
        {
            if (const auto cell = getPerformanceCellAtPosition (event.position, getBuilderGridArea().toFloat()))
            {
                performanceHoverCell = *cell;

                if (performancePlacementMode == PerformancePlacementMode::placeDisc)
                {
                    const auto it = std::find_if (performanceDiscs.begin(), performanceDiscs.end(),
                                                  [cell] (const PerformanceDisc& disc) { return disc.cell == *cell; });
                    if (it != performanceDiscs.end())
                        it->direction = performanceSelectedDirection;
                    else
                        performanceDiscs.push_back ({ *cell, performanceSelectedDirection });

                    performanceSelection = { PerformanceSelection::Kind::disc, *cell };
                    performanceFlashes.push_back ({ *cell, juce::Colour::fromRGBA (255, 208, 112, 255), 1.0f, true });
                }
                else if (performancePlacementMode == PerformancePlacementMode::placeTrack)
                {
                    const auto it = std::find_if (performanceTracks.begin(), performanceTracks.end(),
                                                  [cell] (const PerformanceTrack& track) { return track.cell == *cell; });
                    if (it != performanceTracks.end())
                        it->horizontal = performanceTrackHorizontal;
                    else
                        performanceTracks.push_back ({ *cell, performanceTrackHorizontal });

                    performanceSelection = { PerformanceSelection::Kind::track, *cell };
                    performanceFlashes.push_back ({ *cell, juce::Colour::fromRGBA (120, 220, 255, 255), 0.9f, true });
                }
                else
                {
                    const auto disc = std::find_if (performanceDiscs.begin(), performanceDiscs.end(),
                                                    [cell] (const PerformanceDisc& item) { return item.cell == *cell; });
                    if (disc != performanceDiscs.end())
                    {
                        performanceSelection = { PerformanceSelection::Kind::disc, *cell };
                        performanceSelectedDirection = disc->direction;
                    }
                    else
                    {
                        const auto track = std::find_if (performanceTracks.begin(), performanceTracks.end(),
                                                         [cell] (const PerformanceTrack& item) { return item.cell == *cell; });
                        if (track != performanceTracks.end())
                        {
                            performanceSelection = { PerformanceSelection::Kind::track, *cell };
                            performanceTrackHorizontal = track->horizontal;
                        }
                        else
                        {
                            performanceSelection = {};
                        }
                    }
                }

                repaint();
            }
            return;
        }

        if (topDownBuildMode == TopDownBuildMode::tetris)
        {
            if (getPerformanceCellAtPosition (event.position, getBuilderGridArea().toFloat()).has_value())
            {
                if (! tetrisPiece.active)
                    spawnTetrisPiece (false);

                if (event.mods.isRightButtonDown() || event.mods.isCtrlDown())
                    hardDropTetrisPiece();
                else
                    rotateTetrisPiece();
                repaint();
            }
            return;
        }

        if (topDownBuildMode == TopDownBuildMode::cellularAutomata)
        {
            if (const auto cell = getPerformanceCellAtPosition (event.position, getBuilderGridArea().toFloat()))
            {
                automataHoverCell = *cell;
                const bool remove = event.mods.isRightButtonDown() || event.mods.isCtrlDown();
                toggleAutomataCell (*cell, ! remove);
                repaint();
            }
            return;
        }

        if (builderViewMode == BuilderViewMode::firstPerson)
            return;

        if (event.mods.isRightButtonDown() || event.mods.isCtrlDown())
            applyIsometricPlacement (false);
        else
            applyIsometricPlacement (true);

        repaint();
        return;
    }

    if (event.mods.isLeftButtonDown() || event.mods.isRightButtonDown())
        return;
}

void GameComponent::mouseExit (const juce::MouseEvent&)
{
    if (currentScene == Scene::title && hoveredTitleAction != TitleAction::none)
    {
        hoveredTitleAction = TitleAction::none;
        repaint();
    }
}

bool GameComponent::keyPressed (const juce::KeyPress& key)
{
    const auto lowerChar = juce::CharacterFunctions::toLowerCase (key.getTextCharacter());
    const auto keyCode = key.getKeyCode();

    if (currentScene == Scene::title)
    {
        if (titleSlotOverlayMode != TitleSlotOverlayMode::none)
        {
            if (key == juce::KeyPress::escapeKey)
            {
                closeTitleSlotOverlay();
                return true;
            }

            if (key == juce::KeyPress::upKey && ! titleSaveSlots.empty())
            {
                titleSelectedSlotIndex = juce::jlimit (0, static_cast<int> (titleSaveSlots.size()) - 1,
                                                       titleSelectedSlotIndex <= 0 ? 0 : titleSelectedSlotIndex - 1);
                titleSlotNameDraft = titleSaveSlots[static_cast<size_t> (titleSelectedSlotIndex)].slotName;
                repaint();
                return true;
            }

            if (key == juce::KeyPress::downKey && ! titleSaveSlots.empty())
            {
                titleSelectedSlotIndex = juce::jlimit (0, static_cast<int> (titleSaveSlots.size()) - 1,
                                                       titleSelectedSlotIndex < 0 ? 0 : titleSelectedSlotIndex + 1);
                titleSlotNameDraft = titleSaveSlots[static_cast<size_t> (titleSelectedSlotIndex)].slotName;
                repaint();
                return true;
            }

            if (key == juce::KeyPress::returnKey)
            {
                if (titleSlotOverlayMode == TitleSlotOverlayMode::load)
                {
                    if (titleSelectedSlotIndex >= 0)
                        performTitleLoadFromSlot (titleSelectedSlotIndex);
                }
                else
                {
                    performTitleSaveToSlot (titleSlotNameDraft);
                }
                return true;
            }

            if (titleSlotOverlayMode == TitleSlotOverlayMode::save)
            {
                if (key == juce::KeyPress::backspaceKey)
                {
                    titleSlotNameDraft = titleSlotNameDraft.dropLastCharacters (1);
                    repaint();
                    return true;
                }

                if (keyCode == juce::KeyPress::deleteKey)
                {
                    titleSlotNameDraft.clear();
                    repaint();
                    return true;
                }

                if (keyCode >= 32 && keyCode < 127)
                {
                    const juce::String allowed ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 -_");
                    if (allowed.containsChar (static_cast<juce_wchar> (keyCode)) && titleSlotNameDraft.length() < 28)
                    {
                        titleSlotNameDraft << juce::String::charToString (static_cast<juce_wchar> (keyCode));
                        repaint();
                        return true;
                    }
                }
            }

            return true;
        }

        if (lowerChar == 'r')
        {
            if (isTitleActionEnabled (TitleAction::resumeVoyage))
                setScene (resumeScene);
            return true;
        }
        if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey || lowerChar == 'l')
        {
            openTitleSlotOverlay (TitleSlotOverlayMode::load);
            return true;
        }
        if (lowerChar == 'n')
        {
            enterGalaxyFromTitle (true);
            return true;
        }
        if (lowerChar == 's')
        {
            openTitleSlotOverlay (TitleSlotOverlayMode::save);
            return true;
        }
    }
    else if (currentScene == Scene::galaxy)
    {
        if (lowerChar == 'l')
        {
            galaxyLogOpen = ! galaxyLogOpen;
            if (galaxyLogOpen)
                galaxyLogScroll = 0.0f;
            repaint();
            return true;
        }
        if (galaxyLogOpen && key == juce::KeyPress::escapeKey)
        {
            galaxyLogOpen = false;
            repaint();
            return true;
        }
        if (galaxyLogOpen)
            return true;
        if (key == juce::KeyPress::escapeKey)
        {
            galaxyLogOpen = false;
            setScene (Scene::title);
            return true;
        }
        if (key == juce::KeyPress::leftKey)   { moveSystemSelection (-1); return true; }
        if (key == juce::KeyPress::rightKey)  { moveSystemSelection (1); return true; }
        if (key == juce::KeyPress::upKey)     { movePlanetSelection (-1); return true; }
        if (key == juce::KeyPress::downKey)   { movePlanetSelection (1); return true; }
        if (key == juce::KeyPress::returnKey) { landOnSelectedPlanet(); return true; }
    }
    else if (currentScene == Scene::landing)
    {
        if (key == juce::KeyPress::returnKey || key.getTextCharacter() == ' ')
        {
            enterBuilder();
            return true;
        }

        if (key == juce::KeyPress::escapeKey)
        {
            leavePlanet();
            return true;
        }
    }
    else if (currentScene == Scene::builder)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            performanceMode = false;
            flushPerformanceLogSession();
            saveActivePlanet();
            setScene (Scene::landing);
            return true;
        }

        if (lowerChar == 'p')
        {
            performanceMode = ! performanceMode;
            if (performanceMode)
            {
                performanceEntryView = builderViewMode;
                performanceEntryTopDownMode = topDownBuildMode;
                applyPerformanceEntryDefaults();
                beginPerformanceLogSession();
                if (performanceSnakes.empty() && performanceAutomataCells.empty()
                    && performanceRipples.empty() && performanceSequencers.empty())
                    resetPerformanceAgents();
            }
            else
            {
                flushPerformanceLogSession();
                builderViewMode = performanceEntryView;
                topDownBuildMode = performanceEntryTopDownMode;
            }
            updateFirstPersonMouseCapture();
            repaint();
            return true;
        }

        if (performanceMode)
        {
            if (lowerChar == 'm')
            {
                synthEngine = static_cast<SynthEngine> ((static_cast<int> (synthEngine) + 1) % 6);
                performanceSynthIndex = static_cast<int> (synthEngine);
                repaint();
                return true;
            }
            if (lowerChar == 'b')
            {
                performanceBeatMuted = ! performanceBeatMuted;
                repaint();
                return true;
            }
            if (lowerChar == 'k') { performanceKeyRoot = (performanceKeyRoot + 1) % 12; repaint(); return true; }
            if (lowerChar == 'l')
            {
                scaleType = static_cast<ScaleType> ((static_cast<int> (scaleType) + 1) % 5);
                performanceScaleIndex = static_cast<int> (scaleType);
                repaint();
                return true;
            }
            if (lowerChar == 'h')
            {
                snakeTriggerMode = snakeTriggerMode == SnakeTriggerMode::headOnly
                                     ? SnakeTriggerMode::wholeBody
                                     : SnakeTriggerMode::headOnly;
                repaint();
                return true;
            }
            if (key.getTextCharacter() == ',') { performanceBpm = juce::jlimit (60.0, 220.0, performanceBpm - 2.0); repaint(); return true; }
            if (key.getTextCharacter() == '.') { performanceBpm = juce::jlimit (60.0, 220.0, performanceBpm + 2.0); repaint(); return true; }
            if (lowerChar == 'z')
            {
                performanceRegionMode = (performanceRegionMode + 1) % 3;
                resetPerformanceAgents();
                repaint();
                return true;
            }
            if (lowerChar == 'n')
            {
                performanceAgentMode = static_cast<PerformanceAgentMode> ((static_cast<int> (performanceAgentMode) + 1) % 7);
                resetPerformanceAgents();
                repaint();
                return true;
            }
            if (lowerChar == 'y')
            {
                if (performancePlacementMode == PerformancePlacementMode::placeDisc)
                {
                    static const std::array<juce::Point<int>, 4> directions { juce::Point<int> { 1, 0 }, juce::Point<int> { 0, 1 },
                                                                              juce::Point<int> { -1, 0 }, juce::Point<int> { 0, -1 } };
                    auto it = std::find (directions.begin(), directions.end(), performanceSelectedDirection);
                    const auto index = it != directions.end() ? std::distance (directions.begin(), it) : 0;
                    performanceSelectedDirection = directions[static_cast<size_t> ((index + 1) % 4)];
                }
                else
                {
                    performancePlacementMode = PerformancePlacementMode::placeDisc;
                }
                performanceSelection = {};
                repaint();
                return true;
            }
            if (lowerChar == 't')
            {
                if (performancePlacementMode == PerformancePlacementMode::placeTrack)
                    performanceTrackHorizontal = ! performanceTrackHorizontal;
                performancePlacementMode = PerformancePlacementMode::placeTrack;
                performanceSelection = {};
                repaint();
                return true;
            }
            if (lowerChar == 'u')
            {
                performancePlacementMode = PerformancePlacementMode::selectOnly;
                repaint();
                return true;
            }
            if (lowerChar == 'i' && performanceHoverCell.has_value())
            {
                const auto it = std::find (performanceOrbitCenters.begin(), performanceOrbitCenters.end(), *performanceHoverCell);
                if (it != performanceOrbitCenters.end())
                    performanceOrbitCenters.erase (it);
                else
                    performanceOrbitCenters.push_back (*performanceHoverCell);
                if (performanceAgentMode == PerformanceAgentMode::orbiters)
                    resetPerformanceAgents();
                repaint();
                return true;
            }
            if (key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey)
            {
                if (performanceSelection.kind == PerformanceSelection::Kind::disc)
                    performanceDiscs.erase (std::remove_if (performanceDiscs.begin(), performanceDiscs.end(),
                                                            [this] (const PerformanceDisc& disc) { return disc.cell == performanceSelection.cell; }),
                                            performanceDiscs.end());
                else if (performanceSelection.kind == PerformanceSelection::Kind::track)
                    performanceTracks.erase (std::remove_if (performanceTracks.begin(), performanceTracks.end(),
                                                             [this] (const PerformanceTrack& track) { return track.cell == performanceSelection.cell; }),
                                             performanceTracks.end());
                performanceSelection = {};
                repaint();
                return true;
            }
            if (key == juce::KeyPress::leftKey) { performanceSelectedDirection = { -1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { performanceSelectedDirection = { 1, 0 }; repaint(); return true; }
            if (key == juce::KeyPress::upKey) { performanceSelectedDirection = { 0, -1 }; repaint(); return true; }
            if (key == juce::KeyPress::downKey) { performanceSelectedDirection = { 0, 1 }; repaint(); return true; }
            if (keyCode >= '0' && keyCode <= '8') { setPerformanceAgentCount (keyCode - '0'); repaint(); return true; }
            return true;
        }

        if (key == juce::KeyPress::tabKey)
        {
            if (builderViewMode == BuilderViewMode::isometric && topDownBuildMode == TopDownBuildMode::none)
            {
                builderViewMode = BuilderViewMode::firstPerson;
            }
            else if (builderViewMode == BuilderViewMode::firstPerson)
            {
                builderViewMode = BuilderViewMode::isometric;
                topDownBuildMode = TopDownBuildMode::tetris;
                tetrisBuildLayer = builderLayer;
                spawnTetrisPiece (true);
            }
            else if (topDownBuildMode == TopDownBuildMode::tetris)
            {
                topDownBuildMode = TopDownBuildMode::cellularAutomata;
                automataBuildLayer = tetrisBuildLayer;
                builderLayer = automataBuildLayer;
                automataHoverCell = juce::Point<int> (builderCursorX, builderCursorY);
            }
            else
            {
                topDownBuildMode = TopDownBuildMode::none;
                builderViewMode = BuilderViewMode::isometric;
            }

            if (builderViewMode == BuilderViewMode::firstPerson)
                placeFirstPersonAtSafeSpawn();
            updateFirstPersonMouseCapture();
            syncCursorToFirstPersonTarget();
            repaint();
            return true;
        }

        if (keyCode >= '1' && keyCode <= '4')
        {
            isometricPlacementHeight = keyCode - '0';
            repaint();
            return true;
        }

        if (topDownBuildMode == TopDownBuildMode::tetris)
        {
            if (key == juce::KeyPress::leftKey) { moveTetrisPiece (-1, 0, 0); repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { moveTetrisPiece (1, 0, 0); repaint(); return true; }
            if (key == juce::KeyPress::upKey) { moveTetrisPiece (0, -1, 0); repaint(); return true; }
            if (key == juce::KeyPress::downKey) { moveTetrisPiece (0, 1, 0); repaint(); return true; }
            if (key == juce::KeyPress::pageUpKey || key.getTextCharacter() == ']')
            {
                tetrisBuildLayer = juce::jlimit (1, getSurfaceHeight() - 1, tetrisBuildLayer + 1);
                builderLayer = tetrisBuildLayer;
                if (tetrisPiece.active)
                    tetrisPiece.z = tetrisBuildLayer;
                repaint();
                return true;
            }
            if (key == juce::KeyPress::pageDownKey || key.getTextCharacter() == '[')
            {
                tetrisBuildLayer = juce::jlimit (1, getSurfaceHeight() - 1, tetrisBuildLayer - 1);
                builderLayer = tetrisBuildLayer;
                if (tetrisPiece.active)
                    tetrisPiece.z = tetrisBuildLayer;
                repaint();
                return true;
            }
            if (lowerChar == 'e') { advanceTetrisLayer(); repaint(); return true; }
            if (lowerChar == 'v') { isometricChordType = static_cast<IsometricChordType> ((static_cast<int> (isometricChordType) + 1) % 8); repaint(); return true; }
            if (lowerChar == 'r') { rotateTetrisPiece(); repaint(); return true; }
            if (lowerChar == 'n') { spawnTetrisPiece (true); repaint(); return true; }
            if (lowerChar == 's') { softDropTetrisPiece(); repaint(); return true; }
            if (key == juce::KeyPress::spaceKey) { hardDropTetrisPiece(); repaint(); return true; }
            if (lowerChar == 'o') { placeTetrisPiece (true); repaint(); return true; }
            if (lowerChar == 'x' || key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) { placeTetrisPiece (false); repaint(); return true; }
            if (lowerChar == 'c') { clearPlanetSurface(); repaint(); return true; }
            return true;
        }

        if (topDownBuildMode == TopDownBuildMode::cellularAutomata)
        {
            if (! automataHoverCell.has_value())
                automataHoverCell = juce::Point<int> (builderCursorX, builderCursorY);
            if (key == juce::KeyPress::leftKey) { automataHoverCell = juce::Point<int> (juce::jmax (0, automataHoverCell->x - 1), automataHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::rightKey) { automataHoverCell = juce::Point<int> (juce::jmin (getSurfaceWidth() - 1, automataHoverCell->x + 1), automataHoverCell->y); repaint(); return true; }
            if (key == juce::KeyPress::upKey) { automataHoverCell = juce::Point<int> (automataHoverCell->x, juce::jmax (0, automataHoverCell->y - 1)); repaint(); return true; }
            if (key == juce::KeyPress::downKey) { automataHoverCell = juce::Point<int> (automataHoverCell->x, juce::jmin (getSurfaceDepth() - 1, automataHoverCell->y + 1)); repaint(); return true; }
            if (key == juce::KeyPress::pageUpKey || key.getTextCharacter() == ']') { automataBuildLayer = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer + 1); builderLayer = automataBuildLayer; repaint(); return true; }
            if (key == juce::KeyPress::pageDownKey || key.getTextCharacter() == '[') { automataBuildLayer = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer - 1); builderLayer = automataBuildLayer; repaint(); return true; }
            if (lowerChar == 'n') { randomiseAutomataSeed(); repaint(); return true; }
            if (lowerChar == 'e') { advanceAutomataLayer(); repaint(); return true; }
            if (lowerChar == 'o' || key == juce::KeyPress::spaceKey) { toggleAutomataCell (*automataHoverCell, true); repaint(); return true; }
            if (lowerChar == 'x' || key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) { toggleAutomataCell (*automataHoverCell, false); repaint(); return true; }
            if (lowerChar == 'c') { clearPlanetSurface(); repaint(); return true; }
            return true;
        }

        if (builderViewMode == BuilderViewMode::firstPerson)
        {
            if (key == juce::KeyPress::pageUpKey || key.getTextCharacter() == ']') { firstPersonPlacementOffset = juce::jlimit (1, getSurfaceHeight() - 1, firstPersonPlacementOffset + 1); syncCursorToFirstPersonTarget(); repaint(); return true; }
            if (key == juce::KeyPress::pageDownKey || key.getTextCharacter() == '[') { firstPersonPlacementOffset = juce::jlimit (1, getSurfaceHeight() - 1, firstPersonPlacementOffset - 1); syncCursorToFirstPersonTarget(); repaint(); return true; }

            if (lowerChar == 'c')
            {
                clearPlanetSurface();
                repaint();
                return true;
            }

            return false;
        }

        if (key == juce::KeyPress::leftKey) { moveIsometricCursor (-1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::rightKey) { moveIsometricCursor (1, 0, 0); repaint(); return true; }
        if (key == juce::KeyPress::upKey) { moveIsometricCursor (0, -1, 0); repaint(); return true; }
        if (key == juce::KeyPress::downKey) { moveIsometricCursor (0, 1, 0); repaint(); return true; }
        if (key == juce::KeyPress::pageUpKey || key.getTextCharacter() == ']') { moveIsometricCursor (0, 0, 1); repaint(); return true; }
        if (key == juce::KeyPress::pageDownKey || key.getTextCharacter() == '[') { moveIsometricCursor (0, 0, -1); repaint(); return true; }
        if (key.getTextCharacter() == 'v') { isometricChordType = static_cast<IsometricChordType> ((static_cast<int> (isometricChordType) + 1) % 8); repaint(); return true; }
        if (key.getTextCharacter() == 'p' || key.getTextCharacter() == ' ') { applyIsometricPlacement (true); repaint(); return true; }
        if (key.getTextCharacter() == 'x' || key == juce::KeyPress::backspaceKey || key == juce::KeyPress::deleteKey) { applyIsometricPlacement (false); repaint(); return true; }
        if (key.getTextCharacter() == 'c') { clearPlanetSurface(); repaint(); return true; }
        if (key.getTextCharacter() == 'q') { isometricCamera.rotation = (isometricCamera.rotation + 3) % 4; repaint(); return true; }
        if (key.getTextCharacter() == 'e') { isometricCamera.rotation = (isometricCamera.rotation + 1) % 4; repaint(); return true; }
        if (key.getTextCharacter() == 'w') { isometricCamera.panY += 42.0f; repaint(); return true; }
        if (key.getTextCharacter() == 's') { isometricCamera.panY -= 42.0f; repaint(); return true; }
        if (key.getTextCharacter() == 'a') { isometricCamera.panX += 42.0f; repaint(); return true; }
        if (key.getTextCharacter() == 'd') { isometricCamera.panX -= 42.0f; repaint(); return true; }
        if (key.getTextCharacter() == '-') { isometricCamera.heightScale = juce::jlimit (0.25f, 2.0f, isometricCamera.heightScale - 0.1f); repaint(); return true; }
        if (key.getTextCharacter() == '=') { isometricCamera.heightScale = juce::jlimit (0.25f, 2.0f, isometricCamera.heightScale + 0.1f); repaint(); return true; }
    }

    return false;
}

const StarSystemMetadata& GameComponent::getSelectedSystem() const
{
    return *galaxy.systems.getUnchecked (selectedSystemIndex);
}

const PlanetMetadata& GameComponent::getSelectedPlanet() const
{
    return *getSelectedSystem().planets.getUnchecked (selectedPlanetIndex);
}

void GameComponent::setScene (Scene newScene)
{
    if (newScene != Scene::title)
        resumeScene = newScene;

    currentScene = newScene;
    if (currentScene != Scene::galaxy)
        galaxyLogOpen = false;
    updateMusicState();
    updateFirstPersonMouseCapture();
    repaint();
}

void GameComponent::moveSystemSelection (int delta)
{
    selectedSystemIndex = (selectedSystemIndex + delta + galaxy.systems.size()) % galaxy.systems.size();
    selectedPlanetIndex = 0;
    updateMusicState();
    queueAutosave();
    repaint();
}

void GameComponent::movePlanetSelection (int delta)
{
    const auto& system = getSelectedSystem();
    selectedPlanetIndex = (selectedPlanetIndex + delta + system.planets.size()) % system.planets.size();
    updateMusicState();
    queueAutosave();
    repaint();
}

void GameComponent::landOnSelectedPlanet()
{
    ensureActivePlanetLoaded();
    persistence.recordPlanetVisit (getSelectedSystem(), getSelectedPlanet());
    performanceMode = false;
    resetPerformanceState();
    applyPerformancePresetForPlanet();
    builderCursorX = getSurfaceWidth() / 2;
    builderCursorY = getSurfaceDepth() / 2;
    builderLayer = 1;
    isometricPlacementHeight = 1;
    isometricChordType = IsometricChordType::single;
    firstPersonPlacementOffset = 1;
    tetrisPiece = {};
    nextTetrisType = TetrominoType::L;
    tetrisBuildLayer = 1;
    automataBuildLayer = 1;
    automataHoverCell = {};
    isometricCamera = {};
    hasMouseAnchor = false;
    applyAssignedBuildModeForPlanet();
    queueAutosave();
    setScene (Scene::landing);
}

void GameComponent::enterBuilder()
{
    ensureActivePlanetLoaded();
    performanceMode = false;
    applyPerformancePresetForPlanet();
    firstPersonPlacementOffset = 1;
    tetrisPiece = {};
    nextTetrisType = TetrominoType::L;
    tetrisBuildLayer = 1;
    automataBuildLayer = 1;
    automataHoverCell = {};
    hasMouseAnchor = false;
    updateIsometricCursorFromPosition (getBuilderGridArea().getCentre().toFloat());
    applyAssignedBuildModeForPlanet();
    queueAutosave();
    setScene (Scene::builder);
}

void GameComponent::leavePlanet()
{
    performanceMode = false;
    flushPerformanceLogSession();
    resetPerformanceState();
    saveActivePlanet();
    activePlanetState.reset();
    setScene (Scene::galaxy);
}

void GameComponent::ensureActivePlanetLoaded()
{
    const auto& planet = getSelectedPlanet();

    if (activePlanetState != nullptr && activePlanetState->planetId == planet.id)
        return;

    saveActivePlanet();
    activePlanetState = persistence.loadPlanet (planet.id);

    if (activePlanetState == nullptr)
    {
        activePlanetState = std::make_unique<PlanetSurfaceState> (GalaxyGenerator::generateSurface (planet));
        persistence.savePlanet (*activePlanetState);
    }
}

void GameComponent::saveActivePlanet()
{
    if (activePlanetState != nullptr)
    {
        persistence.savePlanet (*activePlanetState);
        queueAutosave();
    }
}

void GameComponent::beginPerformanceLogSession()
{
    performanceSessionSeconds = 0.0;
    performanceMovementHeat.assign (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()), 0);
    performanceTriggerHeat.assign (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()), 0);
    performanceNoteHeat.assign (static_cast<size_t> (getSurfaceHeight()), 0);
}

void GameComponent::flushPerformanceLogSession()
{
    const bool hasActivity = std::any_of (performanceMovementHeat.begin(), performanceMovementHeat.end(), [] (int v) { return v > 0; })
                          || std::any_of (performanceTriggerHeat.begin(), performanceTriggerHeat.end(), [] (int v) { return v > 0; })
                          || std::any_of (performanceNoteHeat.begin(), performanceNoteHeat.end(), [] (int v) { return v > 0; });

    if (performanceSessionSeconds > 0.0 && hasActivity)
    {
        persistence.recordPerformanceSnapshot (getSelectedSystem(), getSelectedPlanet(),
                                              getSurfaceWidth(), getSurfaceDepth(), performanceSessionSeconds,
                                              performanceMovementHeat, performanceTriggerHeat, performanceNoteHeat);
    }

    performanceSessionSeconds = 0.0;
    performanceMovementHeat.clear();
    performanceTriggerHeat.clear();
    performanceNoteHeat.clear();
}

void GameComponent::recordPerformanceMovementCell (juce::Point<int> cell, int amount)
{
    if (! juce::isPositiveAndBelow (cell.x, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (cell.y, getSurfaceDepth()))
        return;

    const auto index = static_cast<size_t> (cell.y * getSurfaceWidth() + cell.x);
    if (performanceMovementHeat.size() != static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()))
        performanceMovementHeat.assign (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()), 0);
    performanceMovementHeat[index] += amount;
}

void GameComponent::updateMusicState()
{
    const auto& planet = getSelectedPlanet();
    const auto buildMode = planet.assignedBuildMode;
    const auto performanceModeForPlanet = planet.assignedPerformanceMode;
    transportRate = 0.16f + 0.12f * planet.water + (currentScene == Scene::builder ? 0.08f : 0.0f);

    if (performanceMode && currentScene == Scene::builder)
        return;

    if (currentScene == Scene::title)
    {
        synthEngine = SynthEngine::titleBloom;
        scaleType = ScaleType::major;
        drumMode = DrumMode::rezStraight;
    }
    else if (currentScene == Scene::galaxy)
    {
        switch (performanceModeForPlanet)
        {
            case PlanetPerformanceMode::snakes:    synthEngine = SynthEngine::titleBloom;  scaleType = ScaleType::dorian;     drumMode = DrumMode::forwardStep; break;
            case PlanetPerformanceMode::trains:    synthEngine = SynthEngine::digitalV4;   scaleType = ScaleType::minor;      drumMode = DrumMode::railLine;    break;
            case PlanetPerformanceMode::ripple:    synthEngine = SynthEngine::velvetNoise; scaleType = ScaleType::pentatonic; drumMode = DrumMode::tightPulse;  break;
            case PlanetPerformanceMode::sequencer: synthEngine = SynthEngine::fmGlass;     scaleType = ScaleType::chromatic;  drumMode = DrumMode::forwardStep; break;
            case PlanetPerformanceMode::tenori:    synthEngine = SynthEngine::chipPulse;   scaleType = ScaleType::major;      drumMode = DrumMode::rezStraight; break;
        }
    }
    else if (currentScene == Scene::landing)
    {
        switch (buildMode)
        {
            case PlanetBuildMode::isometric:        synthEngine = SynthEngine::fmGlass;     break;
            case PlanetBuildMode::firstPerson:      synthEngine = SynthEngine::guitarPluck; break;
            case PlanetBuildMode::cellularAutomata: synthEngine = SynthEngine::velvetNoise; break;
            case PlanetBuildMode::tetris:           synthEngine = SynthEngine::chipPulse;   break;
        }

        switch (performanceModeForPlanet)
        {
            case PlanetPerformanceMode::snakes:    scaleType = ScaleType::dorian;     drumMode = DrumMode::forwardStep; break;
            case PlanetPerformanceMode::trains:    scaleType = ScaleType::minor;      drumMode = DrumMode::railLine;    break;
            case PlanetPerformanceMode::ripple:    scaleType = ScaleType::pentatonic; drumMode = DrumMode::tightPulse;  break;
            case PlanetPerformanceMode::sequencer: scaleType = ScaleType::chromatic;  drumMode = DrumMode::reactiveBreakbeat; break;
            case PlanetPerformanceMode::tenori:    scaleType = ScaleType::major;      drumMode = DrumMode::rezStraight; break;
        }
    }
    else
    {
        switch (buildMode)
        {
            case PlanetBuildMode::isometric:
                synthEngine = SynthEngine::titleBloom;
                scaleType = ScaleType::major;
                drumMode = DrumMode::rezStraight;
                break;
            case PlanetBuildMode::firstPerson:
                synthEngine = SynthEngine::guitarPluck;
                scaleType = ScaleType::minor;
                drumMode = DrumMode::reactiveBreakbeat;
                break;
            case PlanetBuildMode::cellularAutomata:
                synthEngine = SynthEngine::velvetNoise;
                scaleType = ScaleType::dorian;
                drumMode = DrumMode::tightPulse;
                break;
            case PlanetBuildMode::tetris:
                synthEngine = SynthEngine::chipPulse;
                scaleType = ScaleType::pentatonic;
                drumMode = DrumMode::railLine;
                break;
        }
    }

    performanceSynthIndex = static_cast<int> (synthEngine);
    performanceDrumIndex = static_cast<int> (drumMode);
    performanceScaleIndex = static_cast<int> (scaleType);
    performanceKeyRoot = getAmbientRootMidi() % 12;

    const juce::ScopedLock sl (synthLock);
    ambientStepAccumulator = 0.0;
    ambientStepIndex = 0;
    pendingNoteOffs.clear();
    synth.allNotesOff (0, false);
}

int GameComponent::getAmbientRootMidi() const
{
    const auto hz = juce::jmax (30.0, static_cast<double> (getSelectedPlanet().musicalRootHz));
    const auto midi = static_cast<int> (std::round (69.0 + 12.0 * std::log2 (hz / 440.0)));
    return juce::jlimit (24, 72, quantizePerformanceMidi (midi));
}

std::vector<int> GameComponent::getAmbientChordMidiNotes() const
{
    const int root = getAmbientRootMidi();
    std::vector<int> notes;

    auto add = [&] (int semitones)
    {
        notes.push_back (quantizePerformanceMidi (root + semitones));
    };

    switch (currentScene)
    {
        case Scene::title:
            add (0); add (7); add (12); break;
        case Scene::galaxy:
            add (0); add (7); add (12); add (16); break;
        case Scene::landing:
            add (0); add (3); add (7); add (10); break;
        case Scene::builder:
            add (0); add (5); add (7); add (12); break;
    }

    return notes;
}

int GameComponent::getSurfaceWidth() const noexcept
{
    return activePlanetState != nullptr ? activePlanetState->width : 16;
}

int GameComponent::getSurfaceDepth() const noexcept
{
    return activePlanetState != nullptr ? activePlanetState->depth : 16;
}

int GameComponent::getSurfaceHeight() const noexcept
{
    return PlanetSurfaceState::height;
}

int GameComponent::getTopSolidZAt (int x, int y) const noexcept
{
    if (activePlanetState == nullptr
        || ! juce::isPositiveAndBelow (x, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (y, getSurfaceDepth()))
        return 0;

    for (int z = getSurfaceHeight() - 1; z >= 0; --z)
        if (activePlanetState->getBlock (x, y, z) != 0)
            return z;

    return 0;
}

int GameComponent::getGroundZAt (int x, int y) const noexcept
{
    if (activePlanetState == nullptr
        || ! juce::isPositiveAndBelow (x, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (y, getSurfaceDepth()))
        return 0;

    return activePlanetState->getBlock (x, y, 0) != 0 ? 0 : -1;
}

int GameComponent::getHighestOccupiedZ() const noexcept
{
    if (activePlanetState == nullptr)
        return 0;

    for (int z = getSurfaceHeight() - 1; z >= 0; --z)
        for (int y = 0; y < getSurfaceDepth(); ++y)
            for (int x = 0; x < getSurfaceWidth(); ++x)
                if (activePlanetState->getBlock (x, y, z) != 0)
                    return z;

    return 0;
}

bool GameComponent::isWalkable (float x, float y, float eyeZ) const
{
    const auto cellX = static_cast<int> (std::floor (x));
    const auto cellY = static_cast<int> (std::floor (y));
    const auto feetZ = static_cast<int> (std::floor (eyeZ - 1.2f));
    const auto headZ = static_cast<int> (std::floor (eyeZ - 0.2f));

    if (! juce::isPositiveAndBelow (cellX, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (cellY, getSurfaceDepth()))
        return false;

    if (activePlanetState == nullptr)
        return false;

    const auto emptyAt = [this, cellX, cellY] (int z)
    {
        return ! juce::isPositiveAndBelow (z, getSurfaceHeight())
            || activePlanetState->getBlock (cellX, cellY, z) == 0;
    };

    return emptyAt (feetZ) && emptyAt (headZ);
}

juce::Point<int> GameComponent::rotateIsometricXY (int x, int y) const
{
    switch (isometricCamera.rotation)
    {
        case 1: return { y, getSurfaceWidth() - x };
        case 2: return { getSurfaceWidth() - x, getSurfaceDepth() - y };
        case 3: return { getSurfaceDepth() - y, x };
        default: break;
    }

    return { x, y };
}

juce::Point<float> GameComponent::getIsometricProjectionOffset (juce::Rectangle<float> area) const
{
    constexpr int boardInset = 0;
    const int minXCoord = boardInset;
    const int minYCoord = boardInset;
    const int maxXCoord = getSurfaceWidth();
    const int maxYCoord = getSurfaceDepth();
    const auto tileWidth = refIsoTileWidth * isometricCamera.zoom;
    const auto tileHeight = refIsoTileHeight * isometricCamera.zoom;
    const auto verticalStep = refIsoVerticalStep * isometricCamera.zoom * isometricCamera.heightScale;

    auto projectRaw = [&] (int x, int y, int z)
    {
        const auto rotated = rotateIsometricXY (x, y);
        return juce::Point<float> ((rotated.x - rotated.y) * tileWidth * 0.5f,
                                   (rotated.x + rotated.y) * tileHeight * 0.5f - z * verticalStep);
    };

    const auto occupiedHeight = juce::jlimit (1, getSurfaceHeight(), getHighestOccupiedZ() + 2);

    const std::array<juce::Point<float>, 8> corners {{
        projectRaw (minXCoord, minYCoord, 0),
        projectRaw (maxXCoord, minYCoord, 0),
        projectRaw (minXCoord, maxYCoord, 0),
        projectRaw (maxXCoord, maxYCoord, 0),
        projectRaw (minXCoord, minYCoord, occupiedHeight),
        projectRaw (maxXCoord, minYCoord, occupiedHeight),
        projectRaw (minXCoord, maxYCoord, occupiedHeight),
        projectRaw (maxXCoord, maxYCoord, occupiedHeight)
    }};

    float minX = corners.front().x;
    float maxX = corners.front().x;
    float minY = corners.front().y;
    float maxY = corners.front().y;

    for (const auto& point : corners)
    {
        minX = juce::jmin (minX, point.x);
        maxX = juce::jmax (maxX, point.x);
        minY = juce::jmin (minY, point.y);
        maxY = juce::jmax (maxY, point.y);
    }

    const auto projectedCentre = juce::Point<float> ((minX + maxX) * 0.5f, (minY + maxY) * 0.5f);
    auto targetCentre = area.getCentre();
    targetCentre.y -= area.getHeight() * 0.075f;
    return { targetCentre.x - projectedCentre.x + isometricCamera.panX,
             targetCentre.y - projectedCentre.y + isometricCamera.panY };
}

juce::Point<float> GameComponent::projectIsometricPoint (int x, int y, int z, juce::Rectangle<float> area) const
{
    const auto tileWidth = refIsoTileWidth * isometricCamera.zoom;
    const auto tileHeight = refIsoTileHeight * isometricCamera.zoom;
    const auto verticalStep = refIsoVerticalStep * isometricCamera.zoom * isometricCamera.heightScale;
    const auto offset = getIsometricProjectionOffset (area);
    const auto rotated = rotateIsometricXY (x, y);

    return { offset.x + (rotated.x - rotated.y) * tileWidth * 0.5f,
             offset.y + (rotated.x + rotated.y) * tileHeight * 0.5f - z * verticalStep };
}

int GameComponent::getIsometricGridLineStep() const
{
    if (isometricCamera.zoom < 0.35f)
        return 16;
    if (isometricCamera.zoom < 0.65f)
        return 8;
    if (isometricCamera.zoom < 1.1f)
        return 4;
    return 2;
}

juce::Rectangle<int> GameComponent::getBuilderGridArea() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop (70);
    auto content = bounds.reduced (30);
    auto gridArea = content.removeFromLeft (content.proportionOfWidth (0.64f));
    content.removeFromLeft (16);
    return gridArea.reduced (8);
}

juce::Rectangle<float> GameComponent::getPerformanceBoardBounds (juce::Rectangle<float> area) const
{
    const auto available = area.reduced (36.0f);
    const float tileSize = juce::jmax (10.0f,
                                       juce::jmin (available.getWidth() / static_cast<float> (getSurfaceWidth()),
                                                   available.getHeight() / static_cast<float> (getSurfaceDepth())));
    return juce::Rectangle<float> (tileSize * static_cast<float> (getSurfaceWidth()),
                                   tileSize * static_cast<float> (getSurfaceDepth()))
        .withCentre (area.getCentre());
}

juce::Rectangle<int> GameComponent::getPerformanceRegionBounds() const noexcept
{
    const int width = getSurfaceWidth();
    const int depth = getSurfaceDepth();

    if (performanceRegionMode == 0)
    {
        const int regionWidth = juce::jmax (4, width / 2);
        const int regionDepth = juce::jmax (4, depth / 2);
        return { (width - regionWidth) / 2, (depth - regionDepth) / 2, regionWidth, regionDepth };
    }

    if (performanceRegionMode == 1)
    {
        const int regionWidth = juce::jmax (6, (width * 3) / 4);
        const int regionDepth = juce::jmax (6, (depth * 3) / 4);
        return { (width - regionWidth) / 2, (depth - regionDepth) / 2, regionWidth, regionDepth };
    }

    return { 0, 0, width, depth };
}

std::optional<juce::Point<int>> GameComponent::getPerformanceCellAtPosition (juce::Point<float> position, juce::Rectangle<float> area) const
{
    const auto board = getPerformanceBoardBounds (area);
    if (! board.contains (position))
        return std::nullopt;

    const float tileSize = board.getWidth() / static_cast<float> (getSurfaceWidth());
    const int cellX = juce::jlimit (0, getSurfaceWidth() - 1, static_cast<int> ((position.x - board.getX()) / tileSize));
    const int cellY = juce::jlimit (0, getSurfaceDepth() - 1, static_cast<int> ((position.y - board.getY()) / tileSize));
    return juce::Point<int> (cellX, cellY);
}

void GameComponent::triggerPerformanceNotesAtCell (juce::Point<int> cell)
{
    if (! juce::isPositiveAndBelow (cell.x, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (cell.y, getSurfaceDepth()))
        return;

    if (performanceTriggerHeat.size() != static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()))
        performanceTriggerHeat.assign (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()), 0);
    if (performanceNoteHeat.size() != static_cast<size_t> (getSurfaceHeight()))
        performanceNoteHeat.assign (static_cast<size_t> (getSurfaceHeight()), 0);

    const juce::ScopedLock sl (synthLock);
    const bool titleBloomActive = synthEngine == SynthEngine::titleBloom;
    std::vector<int> hitNotes;
    for (int z = 1; z < getSurfaceHeight(); ++z)
    {
        if (activePlanetState->getBlock (cell.x, cell.y, z) == 0)
            continue;

        const int midiNote = getPerformanceMidiForHeight (z);
        hitNotes.push_back (midiNote);
        ++performanceNoteHeat[static_cast<size_t> (z)];
    }

    if (hitNotes.empty())
        return;

    ++performanceTriggerHeat[static_cast<size_t> (cell.y * getSurfaceWidth() + cell.x)];

    std::sort (hitNotes.begin(), hitNotes.end());
    hitNotes.erase (std::unique (hitNotes.begin(), hitNotes.end()), hitNotes.end());

    int triggered = 0;
    if (titleBloomActive)
    {
        const int beat = beatStepIndex % 16;
        int focalNote = hitNotes.front();

        if (performanceLastImprovMidi >= 0)
        {
            focalNote = *std::min_element (hitNotes.begin(), hitNotes.end(),
                                           [this] (int a, int b)
                                           {
                                               return std::abs (a - performanceLastImprovMidi) < std::abs (b - performanceLastImprovMidi);
                                           });
        }
        else if (hitNotes.size() >= 3)
        {
            focalNote = hitNotes[hitNotes.size() / 2];
        }

        if ((beat % 8) == 4 && hitNotes.size() >= 2)
            focalNote = hitNotes.back();
        else if ((beat % 4) == 0)
            focalNote = hitNotes.front();

        const float velocity = juce::jlimit (0.20f, 0.56f, 0.28f + 0.025f * static_cast<float> (hitNotes.size()));
        synth.noteOn (1, focalNote, velocity);
        schedulePendingNoteOff (pendingNoteOffs, focalNote, 1.30f);

        if ((beat % 8) == 0)
        {
            const int haloNote = quantizePerformanceMidi (focalNote + 12);
            synth.noteOn (1, haloNote, velocity * 0.10f);
            schedulePendingNoteOff (pendingNoteOffs, haloNote, 1.55f);
        }
        triggered = 1;
        performanceLastImprovMidi = focalNote;
    }
    else
    {
        for (size_t i = 0; i < hitNotes.size(); ++i)
        {
            const int midiNote = hitNotes[i];
            const float velocity = juce::jlimit (0.10f, 0.58f, 0.20f + 0.024f * static_cast<float> (i));
            synth.noteOn (1, midiNote, velocity);
            schedulePendingNoteOff (pendingNoteOffs, midiNote, 0.16f);
            ++triggered;
        }
    }

    addPerformanceImprovResponse (hitNotes);
    performanceBeatEnergy = juce::jmin (1.0f, performanceBeatEnergy + 0.08f + 0.03f * static_cast<float> (triggered - 1));
    performanceFlashes.push_back ({ cell, juce::Colour::fromRGBA (120, 220, 255, 255),
                                    juce::jmin (1.0f, 0.70f + 0.12f * static_cast<float> (triggered - 1)), false });
}

void GameComponent::addPerformanceImprovResponse (const std::vector<int>& hitNotes)
{
    if (hitNotes.empty())
        return;

    auto sortedNotes = hitNotes;
    std::sort (sortedNotes.begin(), sortedNotes.end());
    sortedNotes.erase (std::unique (sortedNotes.begin(), sortedNotes.end()), sortedNotes.end());

    performanceRecentHitNotes = sortedNotes;
    ++performanceImprovCounter;

    const int root = sortedNotes.front();
    const int top = sortedNotes.back();
    const int beat = beatStepIndex % 16;
    const int phrase = beatBarIndex % 4;
    const bool denseChord = sortedNotes.size() >= 3;

    auto addImprovNote = [this] (int midiNote, float velocity, float lengthSeconds)
    {
        const int finalMidi = juce::jlimit (0, 127, quantizePerformanceMidi (midiNote));
        synth.noteOn (1, finalMidi, juce::jlimit (0.0f, 1.0f, velocity));
        schedulePendingNoteOff (pendingNoteOffs, finalMidi, lengthSeconds);
        performanceLastImprovMidi = finalMidi;
    };

    const int responseRoot = quantizePerformanceMidi (root + 12);
    const int responseTop = quantizePerformanceMidi (top + (denseChord ? 12 : 7));
    const int responseMid = quantizePerformanceMidi (((root + top) / 2) + 12);
    const int passing = quantizePerformanceMidi (top + ((performanceImprovCounter % 2 == 0) ? 2 : -2));

    if (synthEngine == SynthEngine::titleBloom)
    {
        int chosen = responseRoot;
        switch (beat % 8)
        {
            case 0: chosen = responseRoot; break;
            case 2: chosen = responseMid; break;
            case 4: chosen = responseTop; break;
            case 6: chosen = passing; break;
            default: break;
        }

        if ((beat % 2) == 0)
            addImprovNote (chosen, (beat % 8) == 0 ? 0.18f : 0.13f, (beat % 8) == 0 ? 0.54f : 0.30f);

        return;
    }

    if ((beat % 4) == 0)
        addImprovNote (responseRoot, denseChord ? 0.22f : 0.18f, denseChord ? 0.34f : 0.24f);

    if (denseChord && (beat % 4) == 2)
        addImprovNote (responseMid, 0.18f, 0.22f);

    if ((beat % 8) == 6 || (phrase == 3 && beat == 15))
        addImprovNote (responseTop, 0.16f, 0.18f);

    if (denseChord && performanceBeatEnergy > 0.28f && (performanceImprovCounter % 3) == 0)
        addImprovNote (passing, 0.12f, 0.14f);

    if (synthEngine == SynthEngine::titleBloom)
        addImprovNote (quantizePerformanceMidi (responseRoot + 12), 0.10f, 0.52f);
}

void GameComponent::applyPerformanceEntryDefaults()
{
    const juce::ScopedLock sl (synthLock);
    synth.allNotesOff (0, false);
    beatSynth.allNotesOff (0, false);
    pendingNoteOffs.clear();
    pendingBeatNoteOffs.clear();

    synthEngine = SynthEngine::chipPulse;
    performanceSynthIndex = static_cast<int> (synthEngine);
    scaleType = ScaleType::major;
    performanceScaleIndex = static_cast<int> (scaleType);
    performanceKeyRoot = getAmbientRootMidi() % 12;
    performanceBpm = 116.0;
    drumMode = DrumMode::tightPulse;
    performanceDrumIndex = static_cast<int> (drumMode);
    performanceBeatMuted = true;
    snakeTriggerMode = SnakeTriggerMode::headOnly;
    performanceLastImprovMidi = -1;
    performanceRecentHitNotes.clear();
    performanceImprovCounter = 0;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
}

void GameComponent::resetPerformanceState()
{
    {
        const juce::ScopedLock sl (synthLock);
        synth.allNotesOff (0, false);
        beatSynth.allNotesOff (0, false);
        pendingNoteOffs.clear();
        pendingBeatNoteOffs.clear();
    }

    performanceRegionMode = 2;
    performanceAgentCount = 1;
    performanceAgentMode = PerformanceAgentMode::snakes;
    performanceSnakes.clear();
    performanceDiscs.clear();
    performanceTracks.clear();
    performanceRipples.clear();
    performanceSequencers.clear();
    performanceOrbitCenters.clear();
    performanceAutomataCells.clear();
    performanceFlashes.clear();
    performanceHoverCell.reset();
    performanceSelectedDirection = { 1, 0 };
    performanceTrackHorizontal = true;
    performancePlacementMode = PerformancePlacementMode::selectOnly;
    performanceSelection = {};
    performanceTick = 0;
    performanceTenoriColumn = 0;
    performanceBeatEnergy = 0.0f;
    performanceBpm = 116.0;
    performanceStepAccumulator = 0.0;
    snakeTriggerMode = SnakeTriggerMode::headOnly;
    performanceSynthIndex = static_cast<int> (SynthEngine::titleBloom);
    performanceDrumIndex = 0;
    performanceScaleIndex = 0;
    performanceKeyRoot = 0;
    performanceRecentHitNotes.clear();
    performanceImprovCounter = 0;
    performanceLastImprovMidi = -1;
    performanceBeatMuted = true;
    performanceSessionSeconds = 0.0;
    performanceMovementHeat.clear();
    performanceTriggerHeat.clear();
    performanceNoteHeat.clear();
    synthEngine = SynthEngine::chipPulse;
    drumMode = DrumMode::reactiveBreakbeat;
    scaleType = ScaleType::major;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
}

void GameComponent::applyPerformancePresetForPlanet()
{
    const auto& planet = getSelectedPlanet();
    std::mt19937 rng (planet.id.hashCode());
    std::uniform_int_distribution<int> triggerDist (0, 1);

    switch (planet.assignedPerformanceMode)
    {
        case PlanetPerformanceMode::snakes: performanceAgentMode = PerformanceAgentMode::snakes; break;
        case PlanetPerformanceMode::trains: performanceAgentMode = PerformanceAgentMode::trains; break;
        case PlanetPerformanceMode::ripple: performanceAgentMode = PerformanceAgentMode::ripple; break;
        case PlanetPerformanceMode::sequencer: performanceAgentMode = PerformanceAgentMode::sequencer; break;
        case PlanetPerformanceMode::tenori: performanceAgentMode = PerformanceAgentMode::tenori; break;
    }

    switch (planet.assignedPerformanceMode)
    {
        case PlanetPerformanceMode::snakes:
            performanceBpm = 118.0 + static_cast<double> ((planet.seed & 7) * 2);
            synthEngine = SynthEngine::titleBloom;
            drumMode = DrumMode::forwardStep;
            scaleType = ScaleType::dorian;
            break;
        case PlanetPerformanceMode::trains:
            performanceBpm = 132.0 + static_cast<double> ((planet.seed & 7) * 3);
            synthEngine = SynthEngine::digitalV4;
            drumMode = DrumMode::railLine;
            scaleType = ScaleType::minor;
            break;
        case PlanetPerformanceMode::ripple:
            performanceBpm = 102.0 + static_cast<double> ((planet.seed & 7) * 2);
            synthEngine = SynthEngine::velvetNoise;
            drumMode = DrumMode::tightPulse;
            scaleType = ScaleType::pentatonic;
            break;
        case PlanetPerformanceMode::sequencer:
            performanceBpm = 126.0 + static_cast<double> ((planet.seed & 7) * 2);
            synthEngine = SynthEngine::fmGlass;
            drumMode = DrumMode::reactiveBreakbeat;
            scaleType = ScaleType::chromatic;
            break;
        case PlanetPerformanceMode::tenori:
            performanceBpm = 112.0 + static_cast<double> ((planet.seed & 7) * 2);
            synthEngine = SynthEngine::chipPulse;
            drumMode = DrumMode::rezStraight;
            scaleType = ScaleType::major;
            break;
    }

    switch (planet.assignedBuildMode)
    {
        case PlanetBuildMode::isometric:
            synthEngine = planet.assignedPerformanceMode == PlanetPerformanceMode::snakes ? synthEngine : SynthEngine::titleBloom;
            break;
        case PlanetBuildMode::firstPerson:
            if (planet.assignedPerformanceMode == PlanetPerformanceMode::snakes
                || planet.assignedPerformanceMode == PlanetPerformanceMode::sequencer)
                synthEngine = SynthEngine::guitarPluck;
            performanceBpm += 6.0;
            break;
        case PlanetBuildMode::cellularAutomata:
            scaleType = ScaleType::dorian;
            if (planet.assignedPerformanceMode == PlanetPerformanceMode::tenori)
                synthEngine = SynthEngine::velvetNoise;
            break;
        case PlanetBuildMode::tetris:
            if (planet.assignedPerformanceMode != PlanetPerformanceMode::ripple)
                synthEngine = SynthEngine::chipPulse;
            drumMode = DrumMode::railLine;
            performanceBpm += 10.0;
            break;
    }

    performanceSynthIndex = static_cast<int> (synthEngine);
    performanceDrumIndex = static_cast<int> (drumMode);
    performanceScaleIndex = static_cast<int> (scaleType);
    performanceKeyRoot = getAmbientRootMidi() % 12;
    performanceRecentHitNotes.clear();
    performanceImprovCounter = 0;
    performanceLastImprovMidi = -1;
    performanceBeatMuted = planet.assignedPerformanceMode == PlanetPerformanceMode::ripple;
    snakeTriggerMode = triggerDist (rng) == 0 ? SnakeTriggerMode::headOnly : SnakeTriggerMode::wholeBody;
    beatStepAccumulator = 0.0;
    beatStepIndex = 0;
    beatBarIndex = 0;
    resetPerformanceAgents();
}

void GameComponent::resetPerformanceAgents()
{
    performanceSnakes.clear();
    performanceAutomataCells.clear();
    performanceRipples.clear();
    performanceSequencers.clear();
    performanceFlashes.clear();
    performanceTick = 0;

    const auto bounds = getPerformanceRegionBounds();
    if (bounds.isEmpty() || performanceAgentCount <= 0)
        return;

    juce::Random rng;
    static const std::array<juce::Point<int>, 4> snakeDirections { juce::Point<int> { 1, 0 }, juce::Point<int> { -1, 0 },
                                                                   juce::Point<int> { 0, 1 }, juce::Point<int> { 0, -1 } };
    const int snakeLength = getPerformanceSnakeLength();
    static const std::array<juce::Colour, 8> snakeColours {
        juce::Colour::fromRGB (255, 204, 96), juce::Colour::fromRGB (105, 234, 255),
        juce::Colour::fromRGB (255, 128, 118), juce::Colour::fromRGB (182, 126, 255),
        juce::Colour::fromRGB (110, 214, 114), juce::Colour::fromRGB (255, 168, 84),
        juce::Colour::fromRGB (246, 103, 220), juce::Colour::fromRGB (242, 228, 94)
    };

    if (performanceAgentMode == PerformanceAgentMode::automata)
    {
        const int seedCount = juce::jmax (6, performanceAgentCount * 10);
        for (int i = 0; i < seedCount; ++i)
        {
            juce::Point<int> cell (bounds.getX() + rng.nextInt (bounds.getWidth()),
                                   bounds.getY() + rng.nextInt (bounds.getHeight()));
            if (std::find (performanceAutomataCells.begin(), performanceAutomataCells.end(), cell) == performanceAutomataCells.end())
                performanceAutomataCells.push_back (cell);
        }
        return;
    }

    if (performanceAgentMode == PerformanceAgentMode::ripple)
    {
        const int rippleCount = juce::jmax (1, performanceAgentCount);
        static const std::array<juce::Colour, 8> rippleColours {
            juce::Colour::fromRGB (92, 214, 255), juce::Colour::fromRGB (255, 186, 108),
            juce::Colour::fromRGB (116, 224, 110), juce::Colour::fromRGB (255, 128, 118),
            juce::Colour::fromRGB (182, 126, 255), juce::Colour::fromRGB (255, 214, 84),
            juce::Colour::fromRGB (255, 110, 198), juce::Colour::fromRGB (190, 244, 255)
        };

        for (int i = 0; i < rippleCount; ++i)
        {
            PerformanceRipple ripple;
            ripple.centre = { bounds.getX() + rng.nextInt (bounds.getWidth()),
                              bounds.getY() + rng.nextInt (bounds.getHeight()) };
            ripple.radius = 0;
            ripple.maxRadius = 2 + rng.nextInt (5);
            ripple.colour = rippleColours[static_cast<size_t> (i % static_cast<int> (rippleColours.size()))];
            performanceRipples.push_back (ripple);
        }
        return;
    }

    if (performanceAgentMode == PerformanceAgentMode::sequencer)
    {
        const int sequencerCount = juce::jmax (1, performanceAgentCount);
        static const std::array<juce::Colour, 8> sequencerColours {
            juce::Colour::fromRGB (110, 240, 255), juce::Colour::fromRGB (255, 192, 94),
            juce::Colour::fromRGB (255, 122, 168), juce::Colour::fromRGB (160, 132, 255),
            juce::Colour::fromRGB (122, 232, 118), juce::Colour::fromRGB (255, 144, 104),
            juce::Colour::fromRGB (252, 238, 110), juce::Colour::fromRGB (190, 244, 255)
        };

        for (int i = 0; i < sequencerCount; ++i)
        {
            PerformanceSequencer sequencer;
            const int laneY = bounds.getY() + ((i * juce::jmax (1, bounds.getHeight() - 1))
                                              / juce::jmax (1, sequencerCount - 1));
            sequencer.cell = { bounds.getX(), juce::jlimit (bounds.getY(), bounds.getBottom() - 1, laneY) };
            sequencer.previousCell = sequencer.cell;
            sequencer.direction = { 1, 0 };
            sequencer.colour = sequencerColours[static_cast<size_t> (i % static_cast<int> (sequencerColours.size()))];
            sequencer.hasPreviousCell = false;
            performanceSequencers.push_back (sequencer);
        }
        return;
    }

    if (performanceAgentMode == PerformanceAgentMode::tenori)
    {
        performanceTenoriColumn = bounds.getX();
        return;
    }

    if (performanceAgentMode == PerformanceAgentMode::orbiters && performanceOrbitCenters.empty())
        performanceOrbitCenters.push_back (bounds.getCentre());

    for (int i = 0; i < performanceAgentCount; ++i)
    {
        PerformanceSnake snake;
        snake.colour = snakeColours[static_cast<size_t> (i % static_cast<int> (snakeColours.size()))];
        snake.direction = snakeDirections[static_cast<size_t> (rng.nextInt (static_cast<int> (snakeDirections.size())))];
        snake.clockwise = (i % 2) == 0;

        if (performanceAgentMode == PerformanceAgentMode::orbiters)
        {
            const auto centre = performanceOrbitCenters[static_cast<size_t> (i % static_cast<int> (performanceOrbitCenters.size()))];
            snake.orbitIndex = i % static_cast<int> (performanceOrbitCenters.size());
            const int radius = 2 + (i % 4);
            snake.body.push_back ({ juce::jlimit (bounds.getX(), bounds.getRight() - 1, centre.x + radius),
                                    juce::jlimit (bounds.getY(), bounds.getBottom() - 1, centre.y) });
            snake.direction = snake.clockwise ? juce::Point<int> { 0, 1 } : juce::Point<int> { 0, -1 };
        }
        else
        {
            const auto start = juce::Point<int> (bounds.getX() + rng.nextInt (bounds.getWidth()),
                                                 bounds.getY() + rng.nextInt (bounds.getHeight()));
            snake.body.push_back (start);
            auto tail = start;
            for (int segment = 1; segment < snakeLength; ++segment)
            {
                tail.x = juce::jlimit (bounds.getX(), bounds.getRight() - 1, tail.x - snake.direction.x);
                tail.y = juce::jlimit (bounds.getY(), bounds.getBottom() - 1, tail.y - snake.direction.y);
                snake.body.push_back (tail);
            }
        }

        performanceSnakes.push_back (std::move (snake));
    }
}

void GameComponent::setPerformanceAgentCount (int count)
{
    performanceAgentCount = juce::jlimit (0, 8, count);
    resetPerformanceAgents();
}

bool GameComponent::hasPerformanceTrackAt (juce::Point<int> cell) const noexcept
{
    return std::find_if (performanceTracks.begin(), performanceTracks.end(),
                         [cell] (const PerformanceTrack& track) { return track.cell == cell; }) != performanceTracks.end();
}

bool GameComponent::getPerformanceTrackHorizontalAt (juce::Point<int> cell) const noexcept
{
    const auto it = std::find_if (performanceTracks.begin(), performanceTracks.end(),
                                  [cell] (const PerformanceTrack& track) { return track.cell == cell; });
    return it != performanceTracks.end() ? it->horizontal : true;
}

juce::String GameComponent::getPerformanceAgentModeName() const
{
    switch (performanceAgentMode)
    {
        case PerformanceAgentMode::snakes: return "Snakes";
        case PerformanceAgentMode::trains: return "Trains";
        case PerformanceAgentMode::orbiters: return "Orbiters";
        case PerformanceAgentMode::automata: return "Automata";
        case PerformanceAgentMode::ripple: return "Ripple";
        case PerformanceAgentMode::sequencer: return "Beam";
        case PerformanceAgentMode::tenori: return "Tenori";
    }

    return "Snakes";
}

juce::String GameComponent::getPerformancePlacementModeName() const
{
    switch (performancePlacementMode)
    {
        case PerformancePlacementMode::selectOnly: return "Select";
        case PerformancePlacementMode::placeDisc: return "Disc";
        case PerformancePlacementMode::placeTrack: return performanceTrackHorizontal ? "Track H" : "Track V";
    }

    return "Select";
}

juce::String GameComponent::getSnakeTriggerModeName() const
{
    return snakeTriggerMode == SnakeTriggerMode::headOnly ? "Head only" : "Whole body";
}

int GameComponent::getPerformanceSnakeLength() const noexcept
{
    switch (getSurfaceWidth())
    {
        case 12: return 3;
        case 16: return 4;
        case 20: return 5;
        case 32: return 8;
        default: return 4;
    }
}

juce::String GameComponent::getPerformanceSynthName() const
{
    switch (synthEngine)
    {
        case SynthEngine::digitalV4:   return "Digital V4";
        case SynthEngine::fmGlass:     return "FM Glass";
        case SynthEngine::titleBloom:  return "Title Bloom";
        case SynthEngine::velvetNoise: return "Velvet Noise";
        case SynthEngine::chipPulse:   return "Electric Glockenspiel";
        case SynthEngine::guitarPluck: return "Guitar Pluck";
    }

    return "Digital V4";
}

juce::String GameComponent::getPerformanceDrumName() const
{
    if (performanceBeatMuted)
        return "Beat Off";

    switch (drumMode)
    {
        case DrumMode::reactiveBreakbeat: return "Reactive Breakbeat";
        case DrumMode::rezStraight:       return "Rez Straight";
        case DrumMode::tightPulse:        return "Tight Pulse";
        case DrumMode::forwardStep:       return "Forward Step";
        case DrumMode::railLine:          return "Rail Line";
    }

    return "Reactive Breakbeat";
}

juce::String GameComponent::getPerformanceScaleName() const
{
    switch (scaleType)
    {
        case ScaleType::chromatic:  return "Chromatic";
        case ScaleType::major:      return "Major";
        case ScaleType::minor:      return "Minor";
        case ScaleType::dorian:     return "Dorian";
        case ScaleType::pentatonic: return "Pentatonic";
    }

    return "Chromatic";
}

juce::String GameComponent::getPerformanceKeyName() const
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };
    return names[static_cast<size_t> (juce::jlimit (0, 11, performanceKeyRoot))];
}

std::vector<int> GameComponent::getPerformanceScaleSteps() const
{
    switch (scaleType)
    {
        case ScaleType::chromatic:  return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        case ScaleType::major:      return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleType::minor:      return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleType::dorian:     return { 0, 2, 3, 5, 7, 9, 10 };
        case ScaleType::pentatonic: return { 0, 2, 4, 7, 9 };
    }

    return { 0, 2, 3, 5, 7, 8, 10 };
}

int GameComponent::quantizePerformanceMidi (int midi) const
{
    const auto steps = getPerformanceScaleSteps();
    int bestNote = midi;
    int bestDistance = 128;

    for (int n = midi - 12; n <= midi + 12; ++n)
    {
        const int pc = (n % 12 + 12) % 12;
        const int rel = (pc - performanceKeyRoot + 12) % 12;
        if (std::find (steps.begin(), steps.end(), rel) != steps.end())
        {
            const int d = std::abs (n - midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                bestNote = n;
            }
        }
    }

    return bestNote;
}

int GameComponent::getPerformanceMidiForHeight (int z) const
{
    const int midi = juce::jlimit (0, 72, 23 + z);
    return juce::jlimit (0, 84, quantizePerformanceMidi (midi));
}

void GameComponent::schedulePendingNoteOff (std::vector<PendingNoteOff>& queue, int midiNote, float lengthSeconds)
{
    for (auto& pending : queue)
    {
        if (pending.note == midiNote)
        {
            pending.secondsRemaining = juce::jmax (pending.secondsRemaining, lengthSeconds);
            return;
        }
    }

    queue.push_back ({ midiNote, lengthSeconds });
}

void GameComponent::addBeatEvent (juce::MidiBuffer& buffer, int midiNote, float velocity, int sampleOffset, int blockSamples)
{
    const int onOffset = juce::jlimit (0, juce::jmax (0, blockSamples - 1), sampleOffset);
    buffer.addEvent (juce::MidiMessage::noteOn (1, midiNote, juce::jlimit (0.0f, 1.0f, velocity * 0.74f)), onOffset);
    const float noteLengthSeconds = midiNote == 120 ? 0.12f : midiNote == 121 ? 0.09f : midiNote == 122 ? 0.03f : 0.05f;
    schedulePendingNoteOff (pendingBeatNoteOffs, midiNote, noteLengthSeconds);
}

juce::Rectangle<float> GameComponent::getHotbarBoundsForGridArea (juce::Rectangle<int> area) const
{
    return { static_cast<float> (area.getCentreX()) - 170.0f,
             static_cast<float> (area.getBottom()) - 68.0f,
             340.0f,
             48.0f };
}

juce::Rectangle<float> GameComponent::getHotbarSlotBounds (juce::Rectangle<int> area, int blockType) const
{
    auto bar = getHotbarBoundsForGridArea (area);
    const float slotGap = 10.0f;
    const float slotWidth = (bar.getWidth() - slotGap * 5.0f) / 4.0f;
    auto slot = juce::Rectangle<float> (bar.getX() + slotGap, bar.getY() + 7.0f, slotWidth, bar.getHeight() - 14.0f);
    slot.translate ((blockType - 1) * (slotWidth + slotGap), 0.0f);
    return slot;
}

bool GameComponent::updateIsometricCursorFromPosition (juce::Point<float> position)
{
    if (currentScene != Scene::builder || builderViewMode != BuilderViewMode::isometric || activePlanetState == nullptr)
        return false;

    if (getHotbarBoundsForGridArea (getBuilderGridArea()).contains (position))
        return false;

    const auto area = getBuilderGridArea().toFloat();
    float bestDistance = std::numeric_limits<float>::max();
    juce::Point<int> bestCell;
    bool found = false;

    for (int y = 0; y < getSurfaceDepth(); ++y)
    {
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            juce::Path topFace;
            const auto aTop = projectIsometricPoint (x,     y,     builderLayer + 1, area);
            const auto bTop = projectIsometricPoint (x + 1, y,     builderLayer + 1, area);
            const auto cTop = projectIsometricPoint (x + 1, y + 1, builderLayer + 1, area);
            const auto dTop = projectIsometricPoint (x,     y + 1, builderLayer + 1, area);
            topFace.startNewSubPath (aTop);
            topFace.lineTo (bTop);
            topFace.lineTo (cTop);
            topFace.lineTo (dTop);
            topFace.closeSubPath();

            if (topFace.contains (position))
            {
                bestCell = { x, y };
                found = true;
                bestDistance = 0.0f;
                break;
            }

            const auto centre = juce::Point<float> ((aTop.x + cTop.x) * 0.5f, (aTop.y + cTop.y) * 0.5f);
            const auto distance = centre.getDistanceSquaredFrom (position);
            if (distance < bestDistance)
            {
                bestDistance = distance;
                bestCell = { x, y };
                found = true;
            }
        }
        if (bestDistance == 0.0f)
            break;
    }

    if (! found)
        return false;

    const bool changed = builderCursorX != bestCell.x || builderCursorY != bestCell.y;
    builderCursorX = bestCell.x;
    builderCursorY = bestCell.y;
    return changed;
}

void GameComponent::moveIsometricCursor (int dx, int dy, int dz)
{
    builderCursorX = juce::jlimit (0, getSurfaceWidth() - 1, builderCursorX + dx);
    builderCursorY = juce::jlimit (0, getSurfaceDepth() - 1, builderCursorY + dy);
    builderLayer = juce::jlimit (0, getSurfaceHeight() - 1, builderLayer + dz);
}

std::vector<int> GameComponent::getIsometricChordIntervals() const
{
    switch (isometricChordType)
    {
        case IsometricChordType::single: return { 0 };
        case IsometricChordType::power: return { 0, 7 };
        case IsometricChordType::majorTriad: return { 0, 4, 7 };
        case IsometricChordType::minorTriad: return { 0, 3, 7 };
        case IsometricChordType::sus2: return { 0, 2, 7 };
        case IsometricChordType::sus4: return { 0, 5, 7 };
        case IsometricChordType::majorSeventh: return { 0, 4, 7, 11 };
        case IsometricChordType::minorSeventh: return { 0, 3, 7, 10 };
    }

    return { 0 };
}

std::vector<int> GameComponent::getActiveChordStackIntervals() const
{
    const auto baseIntervals = getIsometricChordIntervals();
    const int desiredCount = juce::jmax (1, isometricPlacementHeight);

    std::vector<int> limited;
    limited.reserve (4);

    for (const int interval : baseIntervals)
    {
        if (interval < 0 || interval > 12)
            continue;
        if (std::find (limited.begin(), limited.end(), interval) == limited.end())
            limited.push_back (interval);
    }

    if (std::find (limited.begin(), limited.end(), 12) == limited.end())
        limited.push_back (12);

    if (static_cast<int> (limited.size()) > desiredCount)
        limited.resize (static_cast<size_t> (desiredCount));

    return limited;
}

juce::String GameComponent::getIsometricChordName() const
{
    switch (isometricChordType)
    {
        case IsometricChordType::single: return "Single";
        case IsometricChordType::power: return "Power";
        case IsometricChordType::majorTriad: return "Major";
        case IsometricChordType::minorTriad: return "Minor";
        case IsometricChordType::sus2: return "Sus2";
        case IsometricChordType::sus4: return "Sus4";
        case IsometricChordType::majorSeventh: return "Maj7";
        case IsometricChordType::minorSeventh: return "Min7";
    }

    return "Single";
}

juce::String GameComponent::getTopDownBuildModeName() const
{
    switch (topDownBuildMode)
    {
        case TopDownBuildMode::none: return builderViewMode == BuilderViewMode::firstPerson ? "First-Person" : "Isometric";
        case TopDownBuildMode::tetris: return "Tetris Build";
        case TopDownBuildMode::cellularAutomata: return "Cellular Automata";
    }

    return "Build";
}

juce::String GameComponent::getPlanetBuildModeName (PlanetBuildMode mode) const
{
    switch (mode)
    {
        case PlanetBuildMode::isometric: return "Isometric";
        case PlanetBuildMode::firstPerson: return "First Person";
        case PlanetBuildMode::cellularAutomata: return "Cellular Automata";
        case PlanetBuildMode::tetris: return "Tetris";
    }

    return "Isometric";
}

juce::Colour GameComponent::getPlanetBuildModeColour (PlanetBuildMode mode) const
{
    switch (mode)
    {
        case PlanetBuildMode::isometric:         return juce::Colour::fromRGB (120, 224, 255);
        case PlanetBuildMode::firstPerson:       return juce::Colour::fromRGB (255, 170, 92);
        case PlanetBuildMode::cellularAutomata:  return juce::Colour::fromRGB (122, 234, 132);
        case PlanetBuildMode::tetris:            return juce::Colour::fromRGB (255, 92, 118);
    }

    return juce::Colour::fromRGB (120, 224, 255);
}

juce::String GameComponent::getPlanetPerformanceModeName (PlanetPerformanceMode mode) const
{
    switch (mode)
    {
        case PlanetPerformanceMode::snakes: return "Snakes";
        case PlanetPerformanceMode::trains: return "Trains";
        case PlanetPerformanceMode::ripple: return "Ripple";
        case PlanetPerformanceMode::sequencer: return "Beam";
        case PlanetPerformanceMode::tenori: return "Tenori";
    }

    return "Snakes";
}

juce::Colour GameComponent::getPlanetPerformanceModeColour (PlanetPerformanceMode mode) const
{
    switch (mode)
    {
        case PlanetPerformanceMode::snakes:    return juce::Colour::fromRGB (255, 212, 102);
        case PlanetPerformanceMode::trains:    return juce::Colour::fromRGB (92, 178, 255);
        case PlanetPerformanceMode::ripple:    return juce::Colour::fromRGB (110, 238, 255);
        case PlanetPerformanceMode::sequencer: return juce::Colour::fromRGB (255, 126, 214);
        case PlanetPerformanceMode::tenori:    return juce::Colour::fromRGB (198, 148, 255);
    }

    return juce::Colour::fromRGB (255, 212, 102);
}

juce::Colour GameComponent::getPlanetIdentityColour (const PlanetMetadata& planet) const
{
    auto modeColour = getPlanetBuildModeColour (planet.assignedBuildMode)
                          .interpolatedWith (getPlanetPerformanceModeColour (planet.assignedPerformanceMode), 0.5f);
    return planet.accent.interpolatedWith (modeColour, 0.55f);
}

juce::String GameComponent::getPlanetBuildModeFlavour (PlanetBuildMode mode) const
{
    switch (mode)
    {
        case PlanetBuildMode::isometric:        return "Board-built terraces and chord architecture";
        case PlanetBuildMode::firstPerson:      return "Ground-level walking, stacking, and excavation";
        case PlanetBuildMode::cellularAutomata: return "Self-propagating lattice growth and mutation";
        case PlanetBuildMode::tetris:           return "Falling slabs, locks, and rhythmic fit";
    }

    return "Board-built terraces and chord architecture";
}

juce::String GameComponent::getPlanetPerformanceModeFlavour (PlanetPerformanceMode mode) const
{
    switch (mode)
    {
        case PlanetPerformanceMode::snakes:    return "Serpentine trails phrase the terrain step by step";
        case PlanetPerformanceMode::trains:    return "Rail voices pulse in straight lines and junctions";
        case PlanetPerformanceMode::ripple:    return "Water rings bloom outward and sing on contact";
        case PlanetPerformanceMode::sequencer: return "Beam sweeps kink into right-angle traversals";
        case PlanetPerformanceMode::tenori:    return "A Tenori column scans the world like a score";
    }

    return "Serpentine trails phrase the terrain step by step";
}

juce::String GameComponent::getPlanetIdentitySummary (const PlanetMetadata& planet) const
{
    return getPlanetBuildModeFlavour (planet.assignedBuildMode) + ". "
         + getPlanetPerformanceModeFlavour (planet.assignedPerformanceMode) + ".";
}

void GameComponent::applyAssignedBuildModeForPlanet()
{
    const auto& planet = getSelectedPlanet();
    topDownBuildMode = TopDownBuildMode::none;
    builderViewMode = BuilderViewMode::isometric;

    switch (planet.assignedBuildMode)
    {
        case PlanetBuildMode::isometric:
            break;
        case PlanetBuildMode::firstPerson:
            builderViewMode = BuilderViewMode::firstPerson;
            placeFirstPersonAtSafeSpawn();
            break;
        case PlanetBuildMode::cellularAutomata:
            topDownBuildMode = TopDownBuildMode::cellularAutomata;
            automataBuildLayer = builderLayer;
            automataHoverCell = juce::Point<int> (builderCursorX, builderCursorY);
            break;
        case PlanetBuildMode::tetris:
            topDownBuildMode = TopDownBuildMode::tetris;
            tetrisBuildLayer = builderLayer;
            spawnTetrisPiece (true);
            break;
    }
}

juce::String GameComponent::getTetrominoTypeName (TetrominoType type) const
{
    switch (type)
    {
        case TetrominoType::I: return "I";
        case TetrominoType::O: return "O";
        case TetrominoType::T: return "T";
        case TetrominoType::L: return "L";
        case TetrominoType::J: return "J";
        case TetrominoType::S: return "S";
        case TetrominoType::Z: return "Z";
    }

    return "T";
}

std::array<juce::Point<int>, 4> GameComponent::getTetrominoOffsets (TetrominoType type, int rotation) const
{
    const int r = ((rotation % 4) + 4) % 4;

    switch (type)
    {
        case TetrominoType::I:
            return r % 2 == 0
                     ? std::array<juce::Point<int>, 4> { juce::Point<int> { -1, 0 }, { 0, 0 }, { 1, 0 }, { 2, 0 } }
                     : std::array<juce::Point<int>, 4> { juce::Point<int> { 0, -1 }, { 0, 0 }, { 0, 1 }, { 0, 2 } };
        case TetrominoType::O:
            return { juce::Point<int> { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
        case TetrominoType::T:
            switch (r)
            {
                case 0: return { juce::Point<int> { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, 1 } };
                case 1: return { juce::Point<int> { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 0 } };
                case 2: return { juce::Point<int> { -1, 0 }, { 0, 0 }, { 1, 0 }, { 0, -1 } };
                default: return { juce::Point<int> { 0, -1 }, { 0, 0 }, { 0, 1 }, { -1, 0 } };
            }
        case TetrominoType::L:
            switch (r)
            {
                case 0: return { juce::Point<int> { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
                case 1: return { juce::Point<int> { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, -1 } };
                case 2: return { juce::Point<int> { -1, -1 }, { -1, 0 }, { 0, 0 }, { 1, 0 } };
                default: return { juce::Point<int> { -1, 1 }, { 0, -1 }, { 0, 0 }, { 0, 1 } };
            }
        case TetrominoType::J:
            switch (r)
            {
                case 0: return { juce::Point<int> { -1, 1 }, { -1, 0 }, { 0, 0 }, { 1, 0 } };
                case 1: return { juce::Point<int> { 0, -1 }, { 0, 0 }, { 0, 1 }, { 1, 1 } };
                case 2: return { juce::Point<int> { -1, 0 }, { 0, 0 }, { 1, 0 }, { 1, -1 } };
                default: return { juce::Point<int> { -1, -1 }, { 0, -1 }, { 0, 0 }, { 0, 1 } };
            }
        case TetrominoType::S:
            return r % 2 == 0
                     ? std::array<juce::Point<int>, 4> { juce::Point<int> { 0, 0 }, { 1, 0 }, { -1, 1 }, { 0, 1 } }
                     : std::array<juce::Point<int>, 4> { juce::Point<int> { 0, -1 }, { 0, 0 }, { 1, 0 }, { 1, 1 } };
        case TetrominoType::Z:
            return r % 2 == 0
                     ? std::array<juce::Point<int>, 4> { juce::Point<int> { -1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } }
                     : std::array<juce::Point<int>, 4> { juce::Point<int> { 1, -1 }, { 0, 0 }, { 1, 0 }, { 0, 1 } };
    }

    return { juce::Point<int> { 0, 0 }, { 1, 0 }, { 0, 1 }, { 1, 1 } };
}

std::vector<juce::Point<int>> GameComponent::getTetrisPlacementCells (const TetrisPiece& piece) const
{
    std::vector<juce::Point<int>> cells;
    cells.reserve (4);
    for (const auto& offset : getTetrominoOffsets (piece.type, piece.rotation))
        cells.push_back (piece.anchor + offset);
    return cells;
}

bool GameComponent::tetrisPieceFits (const TetrisPiece& piece) const
{
    if (activePlanetState == nullptr || ! piece.active)
        return false;

    const auto intervals = getActiveChordStackIntervals();
    if (intervals.empty())
        return false;

    for (const auto& cell : getTetrisPlacementCells (piece))
    {
        if (! juce::isPositiveAndBelow (cell.x, getSurfaceWidth()) || ! juce::isPositiveAndBelow (cell.y, getSurfaceDepth()))
            return false;

        for (const int interval : intervals)
        {
            const int noteZ = piece.z + interval;
            if (noteZ < 1 || noteZ >= getSurfaceHeight())
                return false;
        }
    }

    return true;
}

bool GameComponent::tetrisPieceCollidesWithVoxels (const TetrisPiece& piece) const
{
    if (activePlanetState == nullptr || ! piece.active || ! tetrisPieceFits (piece))
        return false;

    const auto intervals = getActiveChordStackIntervals();
    for (const auto& cell : getTetrisPlacementCells (piece))
        for (const int interval : intervals)
        {
            const int noteZ = piece.z + interval;
            if (activePlanetState->getBlock (cell.x, cell.y, noteZ) != 0)
                return true;
        }

    return false;
}

void GameComponent::clampTetrisPieceToSurface (TetrisPiece& piece) const
{
    const auto offsets = getTetrominoOffsets (piece.type, piece.rotation);
    int minX = std::numeric_limits<int>::max();
    int minY = std::numeric_limits<int>::max();
    int maxX = std::numeric_limits<int>::min();
    int maxY = std::numeric_limits<int>::min();

    for (const auto& offset : offsets)
    {
        minX = juce::jmin (minX, offset.x);
        minY = juce::jmin (minY, offset.y);
        maxX = juce::jmax (maxX, offset.x);
        maxY = juce::jmax (maxY, offset.y);
    }

    piece.anchor.x = juce::jlimit (-minX, getSurfaceWidth() - 1 - maxX, piece.anchor.x);
    piece.anchor.y = juce::jlimit (-minY, getSurfaceDepth() - 1 - maxY, piece.anchor.y);
    piece.z = juce::jlimit (1, getSurfaceHeight() - 1, piece.z);
}

GameComponent::TetrominoType GameComponent::getRandomTetrominoType() const
{
    return static_cast<TetrominoType> (juce::Random::getSystemRandom().nextInt (7));
}

void GameComponent::spawnTetrisPiece (bool randomizeType)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    if (randomizeType)
        nextTetrisType = getRandomTetrominoType();

    tetrisPiece.type = nextTetrisType;
    tetrisPiece.rotation = 0;
    tetrisPiece.anchor = { getSurfaceWidth() / 2, 0 };
    tetrisPiece.z = juce::jlimit (1, getSurfaceHeight() - 1, tetrisBuildLayer);
    tetrisPiece.active = true;
    clampTetrisPieceToSurface (tetrisPiece);

    if (tetrisPieceCollidesWithVoxels (tetrisPiece))
    {
        const int spawnRow = tetrisPiece.anchor.y;
        bool foundOpenSpawn = false;

        for (int radius = 1; radius < getSurfaceWidth() && ! foundOpenSpawn; ++radius)
            for (int dir : { -1, 1 })
            {
                auto candidate = tetrisPiece;
                candidate.anchor.x = (getSurfaceWidth() / 2) + dir * radius;
                candidate.anchor.y = spawnRow;
                clampTetrisPieceToSurface (candidate);

                if (tetrisPieceFits (candidate) && ! tetrisPieceCollidesWithVoxels (candidate))
                {
                    tetrisPiece = candidate;
                    foundOpenSpawn = true;
                    break;
                }
            }

        if (! foundOpenSpawn && tetrisPieceCollidesWithVoxels (tetrisPiece))
            tetrisPiece.active = false;
    }

    nextTetrisType = getRandomTetrominoType();
    tetrisGravityTick = 0;
}

void GameComponent::moveTetrisPiece (int dx, int dy, int dz)
{
    if (! tetrisPiece.active)
        spawnTetrisPiece (false);

    auto moved = tetrisPiece;
    moved.anchor += juce::Point<int> (dx, dy);
    moved.z += dz;
    clampTetrisPieceToSurface (moved);

    if (moved.anchor == tetrisPiece.anchor && moved.z == tetrisPiece.z)
        return;

    if (tetrisPieceFits (moved) && ! tetrisPieceCollidesWithVoxels (moved))
        tetrisPiece = moved;
}

void GameComponent::rotateTetrisPiece()
{
    if (! tetrisPiece.active)
        spawnTetrisPiece (false);

    auto rotated = tetrisPiece;
    rotated.rotation = (rotated.rotation + 1) % 4;
    clampTetrisPieceToSurface (rotated);

    if (tetrisPieceFits (rotated) && ! tetrisPieceCollidesWithVoxels (rotated))
        tetrisPiece = rotated;
}

void GameComponent::placeTetrisPiece (bool filled)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    if (! tetrisPiece.active)
        spawnTetrisPiece (false);

    if (! tetrisPieceFits (tetrisPiece))
        return;

    if (filled && tetrisPieceCollidesWithVoxels (tetrisPiece))
        return;

    const auto intervals = getActiveChordStackIntervals();
    for (const auto& cell : getTetrisPlacementCells (tetrisPiece))
    {
        for (const int interval : intervals)
        {
            const int z = juce::jlimit (1, getSurfaceHeight() - 1, tetrisPiece.z + interval);
            const int mappedBlockType = 1 + (z % 4);
            activePlanetState->setBlock (cell.x, cell.y, z, filled ? mappedBlockType : 0);
        }
    }

    saveActivePlanet();
    spawnTetrisPiece (true);
}

void GameComponent::softDropTetrisPiece()
{
    if (! tetrisPiece.active)
    {
        spawnTetrisPiece (true);
        return;
    }

    auto dropped = tetrisPiece;
    dropped.anchor.y += 1;
    clampTetrisPieceToSurface (dropped);

    const bool movedDown = dropped.anchor.y != tetrisPiece.anchor.y;
    if (movedDown && tetrisPieceFits (dropped) && ! tetrisPieceCollidesWithVoxels (dropped))
    {
        tetrisPiece = dropped;
        return;
    }

    if (tetrisPieceFits (tetrisPiece) && ! tetrisPieceCollidesWithVoxels (tetrisPiece))
        placeTetrisPiece (true);
}

void GameComponent::hardDropTetrisPiece()
{
    if (! tetrisPiece.active)
        spawnTetrisPiece (true);

    while (true)
    {
        auto dropped = tetrisPiece;
        dropped.anchor.y += 1;
        clampTetrisPieceToSurface (dropped);

        if (dropped.anchor.y == tetrisPiece.anchor.y || ! tetrisPieceFits (dropped) || tetrisPieceCollidesWithVoxels (dropped))
            break;

        tetrisPiece = dropped;
    }

    if (tetrisPieceFits (tetrisPiece) && ! tetrisPieceCollidesWithVoxels (tetrisPiece))
        placeTetrisPiece (true);
}

void GameComponent::advanceTetrisLayer()
{
    tetrisBuildLayer = juce::jlimit (1, getSurfaceHeight() - 1, tetrisBuildLayer + 1);
    builderLayer = tetrisBuildLayer;
    spawnTetrisPiece (true);
}

int GameComponent::getAutomataNeighbourCount (int x, int y, int z) const
{
    int count = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dx = -1; dx <= 1; ++dx)
        {
            if (dx == 0 && dy == 0)
                continue;

            const int nx = x + dx;
            const int ny = y + dy;
            if (juce::isPositiveAndBelow (nx, getSurfaceWidth())
                && juce::isPositiveAndBelow (ny, getSurfaceDepth())
                && activePlanetState != nullptr
                && activePlanetState->getBlock (nx, ny, z) != 0)
                ++count;
        }

    return count;
}

void GameComponent::toggleAutomataCell (juce::Point<int> cell, bool filled)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr
        || ! juce::isPositiveAndBelow (cell.x, getSurfaceWidth())
        || ! juce::isPositiveAndBelow (cell.y, getSurfaceDepth()))
        return;

    const int z = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer);
    const int mappedBlockType = 1 + (z % 4);
    activePlanetState->setBlock (cell.x, cell.y, z, filled ? mappedBlockType : 0);
    saveActivePlanet();
}

void GameComponent::randomiseAutomataSeed()
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const int z = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer);
    juce::Random rng (static_cast<int64> (getSelectedPlanet().seed) + z * 971);
    for (int y = 0; y < getSurfaceDepth(); ++y)
        for (int x = 0; x < getSurfaceWidth(); ++x)
            activePlanetState->setBlock (x, y, z, rng.nextFloat() < 0.28f ? 1 + (z % 4) : 0);
    saveActivePlanet();
}

void GameComponent::advanceAutomataLayer()
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const int sourceZ = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer);
    const int destZ = juce::jlimit (1, getSurfaceHeight() - 1, sourceZ + 1);
    if (destZ == sourceZ)
        return;

    std::vector<int> nextLayer (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth()), 0);
    for (int y = 0; y < getSurfaceDepth(); ++y)
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            const bool alive = activePlanetState->getBlock (x, y, sourceZ) != 0;
            const int count = getAutomataNeighbourCount (x, y, sourceZ);
            bool nextAlive = false;

            switch ((sourceZ + x + y) % 4)
            {
                case 0: nextAlive = (alive && (count == 2 || count == 3)) || (! alive && count == 3); break;
                case 1: nextAlive = (alive && (count >= 4 && count <= 8)) || (! alive && count == 3); break;
                case 2: nextAlive = (alive && (count % 2 == 1)) || (! alive && (count % 2 == 1)); break;
                default: nextAlive = (alive && (count == 3 || count == 4 || count == 6 || count == 7 || count == 8))
                                  || (! alive && (count == 3 || count == 6 || count == 7 || count == 8)); break;
            }

            nextLayer[static_cast<size_t> (y * getSurfaceWidth() + x)] = nextAlive ? 1 + (destZ % 4) : 0;
        }

    for (int y = 0; y < getSurfaceDepth(); ++y)
        for (int x = 0; x < getSurfaceWidth(); ++x)
            activePlanetState->setBlock (x, y, destZ,
                                         nextLayer[static_cast<size_t> (y * getSurfaceWidth() + x)]);

    automataBuildLayer = destZ;
    builderLayer = automataBuildLayer;
    saveActivePlanet();
}

void GameComponent::placeFirstPersonAtSafeSpawn()
{
    if (activePlanetState == nullptr)
        return;

    constexpr float eyeHeight = 2.35f;
    const int centreX = getSurfaceWidth() / 2;
    const int centreY = getSurfaceDepth() / 2;

    auto topSolidAt = [this] (int x, int y)
    {
        for (int z = getSurfaceHeight() - 1; z >= 0; --z)
            if (activePlanetState->getBlock (x, y, z) != 0)
                return z;

        return -1;
    };

    auto trySpawnAt = [this, &topSolidAt] (int x, int y) -> bool
    {
        if (! juce::isPositiveAndBelow (x, getSurfaceWidth())
            || ! juce::isPositiveAndBelow (y, getSurfaceDepth()))
            return false;

        constexpr float eyeHeightLocal = 2.35f;
        const float eyeZ = static_cast<float> (topSolidAt (x, y)) + eyeHeightLocal;
        const float posX = static_cast<float> (x) + 0.5f;
        const float posY = static_cast<float> (y) + 0.5f;
        if (! isWalkable (posX, posY, eyeZ))
            return false;

        firstPersonState.x = posX;
        firstPersonState.y = posY;
        firstPersonState.eyeZ = eyeZ;
        firstPersonState.verticalVelocity = 0.0f;
        return true;
    };

    for (int radius = 0; radius < juce::jmax (getSurfaceWidth(), getSurfaceDepth()); ++radius)
    {
        for (int y = centreY - radius; y <= centreY + radius; ++y)
            if (trySpawnAt (centreX - radius, y) || trySpawnAt (centreX + radius, y))
                return;

        for (int x = centreX - radius + 1; x <= centreX + radius - 1; ++x)
            if (trySpawnAt (x, centreY - radius) || trySpawnAt (x, centreY + radius))
                return;
    }

    firstPersonState.x = static_cast<float> (centreX) + 0.5f;
    firstPersonState.y = static_cast<float> (centreY) + 0.5f;
    firstPersonState.eyeZ = eyeHeight;
    firstPersonState.verticalVelocity = 0.0f;
}

void GameComponent::applyIsometricPlacement (bool filled)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const auto intervals = getActiveChordStackIntervals();
    for (const int interval : intervals)
    {
        const int z = builderLayer + interval;
        if (! juce::isPositiveAndBelow (z, getSurfaceHeight()))
            continue;

        if (! filled && z == 0)
            continue;

        const int mappedBlockType = 1 + (z % 4);
        activePlanetState->setBlock (builderCursorX, builderCursorY, z, filled ? mappedBlockType : 0);
    }

    saveActivePlanet();
}

void GameComponent::clearPlanetSurface()
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    for (int y = 0; y < getSurfaceDepth(); ++y)
        for (int x = 0; x < getSurfaceWidth(); ++x)
            activePlanetState->setBlock (x, y, 0, 1);

    for (int z = 1; z < getSurfaceHeight(); ++z)
        for (int y = 0; y < getSurfaceDepth(); ++y)
            for (int x = 0; x < getSurfaceWidth(); ++x)
                activePlanetState->setBlock (x, y, z, 0);

    saveActivePlanet();
}

void GameComponent::syncCursorToFirstPersonTarget()
{
    if (builderViewMode != BuilderViewMode::firstPerson || activePlanetState == nullptr)
        return;

    const auto target = findFirstPersonTarget();
    if (target.valid)
    {
        builderCursorX = target.hitX;
        builderCursorY = target.hitY;
        builderLayer = juce::jlimit (1, getSurfaceHeight() - 1, getGroundZAt (target.hitX, target.hitY) + firstPersonPlacementOffset);
    }
    else
    {
        builderCursorX = juce::jlimit (0, getSurfaceWidth() - 1, static_cast<int> (std::floor (firstPersonState.x)));
        builderCursorY = juce::jlimit (0, getSurfaceDepth() - 1, static_cast<int> (std::floor (firstPersonState.y)));
        builderLayer = juce::jlimit (1, getSurfaceHeight() - 1, getGroundZAt (builderCursorX, builderCursorY) + firstPersonPlacementOffset);
    }
}

GameComponent::TargetedVoxel GameComponent::findFirstPersonTarget() const
{
    TargetedVoxel target;
    if (activePlanetState == nullptr)
        return target;

    const float originX = firstPersonState.x;
    const float originY = firstPersonState.y;
    const float originZ = firstPersonState.eyeZ;
    const float cosPitch = std::cos (firstPersonState.pitch);
    const float dirX = std::sin (firstPersonState.yaw) * cosPitch;
    const float dirY = std::cos (firstPersonState.yaw) * cosPitch;
    const float dirZ = std::sin (firstPersonState.pitch);

    constexpr float maxDistance = 12.0f;
    constexpr float stepSize = 0.05f;

    for (float distance = 0.0f; distance <= maxDistance; distance += stepSize)
    {
        const auto sampleX = originX + dirX * distance;
        const auto sampleY = originY + dirY * distance;
        const auto sampleZ = originZ + dirZ * distance;
        const auto cellX = static_cast<int> (std::floor (sampleX));
        const auto cellY = static_cast<int> (std::floor (sampleY));
        if (! juce::isPositiveAndBelow (cellX, getSurfaceWidth())
            || ! juce::isPositiveAndBelow (cellY, getSurfaceDepth()))
            continue;

        const int groundZ = getGroundZAt (cellX, cellY);
        const int topSolidZ = getTopSolidZAt (cellX, cellY);
        const float topSurface = static_cast<float> (topSolidZ + 1);
        const float groundTop = static_cast<float> (groundZ + 1);
        const bool hitsStackTop = topSolidZ > groundZ && sampleZ <= topSurface + 0.02f && sampleZ >= topSurface - 0.28f;
        const bool hitsGround = sampleZ <= groundTop + 0.02f;

        if (hitsStackTop || hitsGround)
        {
            target.valid = true;
            target.hitX = cellX;
            target.hitY = cellY;
            target.hitZ = topSolidZ;
            target.placeX = cellX;
            target.placeY = cellY;
            target.placeZ = hitsStackTop
                                ? juce::jlimit (1, getSurfaceHeight() - 1, topSolidZ + 1)
                                : juce::jlimit (1, getSurfaceHeight() - 1, groundZ + firstPersonPlacementOffset);
            return target;
        }
    }

    return target;
}

void GameComponent::applyFirstPersonAction (bool placeBlock)
{
    if (currentScene != Scene::builder || builderViewMode != BuilderViewMode::firstPerson || activePlanetState == nullptr)
        return;

    const auto target = findFirstPersonTarget();
    if (! target.valid)
        return;

    if (placeBlock)
    {
        const int placeX = target.placeX;
        const int placeY = target.placeY;
        const int baseZ = target.placeZ;

        if (! juce::isPositiveAndBelow (placeX, getSurfaceWidth())
            || ! juce::isPositiveAndBelow (placeY, getSurfaceDepth())
            || ! juce::isPositiveAndBelow (baseZ, getSurfaceHeight()))
            return;

        const int mappedBlockType = 1 + (baseZ % 4);
        if (activePlanetState->getBlock (placeX, placeY, baseZ) == mappedBlockType)
            return;

        activePlanetState->setBlock (placeX, placeY, baseZ, mappedBlockType);
        builderCursorX = placeX;
        builderCursorY = placeY;
        builderLayer = baseZ;
    }
    else
    {
        if (activePlanetState->getBlock (target.hitX, target.hitY, target.hitZ) == 0)
            return;

        if (target.hitZ == 0)
            return;

        activePlanetState->setBlock (target.hitX, target.hitY, target.hitZ, 0);

        builderCursorX = target.hitX;
        builderCursorY = target.hitY;
        builderLayer = target.hitZ;
    }

    saveActivePlanet();
    syncCursorToFirstPersonTarget();
    repaint();
}

void GameComponent::updateFirstPersonMouseCapture()
{
    const bool shouldCapture = currentScene == Scene::builder
                            && builderViewMode == BuilderViewMode::firstPerson
                            && ! performanceMode;
    setMouseCursor (shouldCapture ? juce::MouseCursor::NoCursor : juce::MouseCursor::NormalCursor);
    firstPersonCursorCaptured = shouldCapture;
    hasMouseAnchor = false;
    suppressNextMouseMove = shouldCapture;

    if (shouldCapture)
    {
        if (! hasKeyboardFocus (true))
            grabKeyboardFocus();
        recenterFirstPersonMouse();
    }
}

void GameComponent::recenterFirstPersonMouse()
{
    if (! isShowing())
        return;

    const auto centre = getLocalBounds().getCentre().toFloat();
    const auto screen = localPointToGlobal (centre).toFloat();
    auto mouse = juce::Desktop::getInstance().getMainMouseSource();
    mouse.setScreenPosition (screen);
    lastMousePosition = centre;
}

juce::String GameComponent::getSceneTitle() const
{
    switch (currentScene)
    {
        case Scene::title:   return "Title Screen";
        case Scene::galaxy:  return "Galaxy Navigation";
        case Scene::landing: return "Orbital Landing";
        case Scene::builder: return builderViewMode == BuilderViewMode::firstPerson ? "First-Person Builder" : "Isometric Build Board";
    }

    return {};
}

juce::String GameComponent::getBuilderViewName() const
{
    return builderViewMode == BuilderViewMode::firstPerson ? "The War Below First Person" : "Reference Isometric";
}

juce::Rectangle<float> GameComponent::titleCardBounds (juce::Rectangle<float> area) const
{
    const auto width = juce::jmin (area.getWidth() - 64.0f, 1100.0f);
    const auto height = juce::jmin (area.getHeight() - 120.0f, 470.0f);
    return area.withSizeKeepingCentre (width, height).translated (0.0f, 8.0f);
}

juce::Rectangle<float> GameComponent::getTitleInteractionArea() const
{
    return getLocalBounds().reduced (30).toFloat();
}

juce::Rectangle<float> GameComponent::titleButtonBounds (juce::Rectangle<float> area, int index) const
{
    auto row = titleCardBounds (area).reduced (42.0f, 34.0f);
    row.removeFromTop (row.getHeight() - 146.0f);
    const float gap = 18.0f;
    const float buttonWidth = (row.getWidth() - gap * 3.0f) / 4.0f;
    row.removeFromLeft ((buttonWidth + gap) * static_cast<float> (index));
    return row.removeFromLeft (buttonWidth).removeFromTop (124.0f);
}

GameComponent::TitleAction GameComponent::titleActionAt (juce::Point<float> position, juce::Rectangle<float> area) const
{
    const std::array<TitleAction, 4> actions {
        TitleAction::resumeVoyage,
        TitleAction::newVoyage,
        TitleAction::loadVoyage,
        TitleAction::saveVoyage
    };

    for (int i = 0; i < 4; ++i)
        if (titleButtonBounds (area, i).expanded (10.0f, 8.0f).contains (position) && isTitleActionEnabled (actions[static_cast<size_t> (i)]))
            return actions[static_cast<size_t> (i)];

    return TitleAction::none;
}

juce::String GameComponent::titleActionLabel (TitleAction action) const
{
    switch (action)
    {
        case TitleAction::resumeVoyage: return "RESUME";
        case TitleAction::loadVoyage: return "LOAD";
        case TitleAction::newVoyage: return "NEW";
        case TitleAction::saveVoyage: return "SAVE";
        case TitleAction::none: break;
    }

    return {};
}

bool GameComponent::isTitleActionEnabled (TitleAction action) const
{
    if (action == TitleAction::resumeVoyage)
        return titleResumeAvailable;

    return true;
}

void GameComponent::enterGalaxyFromTitle (bool regenerateGalaxy)
{
    if (regenerateGalaxy)
    {
        saveActivePlanet();
        galaxy = GalaxyGenerator::generateGalaxy (juce::Random::getSystemRandom().nextInt());
        persistence.clearWorkingData();
        selectedSystemIndex = 0;
        selectedPlanetIndex = 0;
        activePlanetState.reset();
        builderViewMode = BuilderViewMode::isometric;
        topDownBuildMode = TopDownBuildMode::none;
        performanceMode = false;
        titleSlotNameDraft = galaxy.name;
    }

    titleResumeAvailable = true;
    hoveredTitleAction = TitleAction::none;
    closeTitleSlotOverlay();
    queueAutosave();
    setScene (Scene::galaxy);
}

juce::var GameComponent::serialiseVoyageSession() const
{
    auto* object = new juce::DynamicObject();
    object->setProperty ("currentScene", static_cast<int> (currentScene));
    object->setProperty ("resumeScene", static_cast<int> (resumeScene));
    object->setProperty ("selectedSystemIndex", selectedSystemIndex);
    object->setProperty ("selectedPlanetIndex", selectedPlanetIndex);
    object->setProperty ("titleResumeAvailable", titleResumeAvailable);
    object->setProperty ("builderViewMode", static_cast<int> (builderViewMode));
    object->setProperty ("topDownBuildMode", static_cast<int> (topDownBuildMode));
    object->setProperty ("builderCursorX", builderCursorX);
    object->setProperty ("builderCursorY", builderCursorY);
    object->setProperty ("builderLayer", builderLayer);
    object->setProperty ("isometricPlacementHeight", isometricPlacementHeight);
    object->setProperty ("isometricChordType", static_cast<int> (isometricChordType));
    object->setProperty ("firstPersonPlacementOffset", firstPersonPlacementOffset);
    object->setProperty ("performanceMode", performanceMode);
    object->setProperty ("performanceEntryView", static_cast<int> (performanceEntryView));
    object->setProperty ("performanceEntryTopDownMode", static_cast<int> (performanceEntryTopDownMode));
    object->setProperty ("tetrisBuildLayer", tetrisBuildLayer);
    object->setProperty ("automataBuildLayer", automataBuildLayer);
    object->setProperty ("performanceRegionMode", performanceRegionMode);
    object->setProperty ("performanceAgentCount", performanceAgentCount);
    object->setProperty ("performanceAgentMode", static_cast<int> (performanceAgentMode));
    object->setProperty ("performancePlacementMode", static_cast<int> (performancePlacementMode));
    object->setProperty ("performanceTrackHorizontal", performanceTrackHorizontal);
    object->setProperty ("performanceSelectedDirectionX", performanceSelectedDirection.x);
    object->setProperty ("performanceSelectedDirectionY", performanceSelectedDirection.y);
    object->setProperty ("performanceTick", performanceTick);
    object->setProperty ("performanceTenoriColumn", performanceTenoriColumn);
    object->setProperty ("performanceBpm", performanceBpm);
    object->setProperty ("performanceBeatMuted", performanceBeatMuted);
    object->setProperty ("snakeTriggerMode", static_cast<int> (snakeTriggerMode));
    object->setProperty ("synthEngine", static_cast<int> (synthEngine));
    object->setProperty ("drumMode", static_cast<int> (drumMode));
    object->setProperty ("scaleType", static_cast<int> (scaleType));
    object->setProperty ("performanceKeyRoot", performanceKeyRoot);
    object->setProperty ("performanceImprovCounter", performanceImprovCounter);
    object->setProperty ("performanceLastImprovMidi", performanceLastImprovMidi);

    auto* fp = new juce::DynamicObject();
    fp->setProperty ("x", firstPersonState.x);
    fp->setProperty ("y", firstPersonState.y);
    fp->setProperty ("eyeZ", firstPersonState.eyeZ);
    fp->setProperty ("verticalVelocity", firstPersonState.verticalVelocity);
    fp->setProperty ("yaw", firstPersonState.yaw);
    fp->setProperty ("pitch", firstPersonState.pitch);
    object->setProperty ("firstPersonState", juce::var (fp));

    auto* iso = new juce::DynamicObject();
    iso->setProperty ("rotation", isometricCamera.rotation);
    iso->setProperty ("zoom", isometricCamera.zoom);
    iso->setProperty ("heightScale", isometricCamera.heightScale);
    iso->setProperty ("panX", isometricCamera.panX);
    iso->setProperty ("panY", isometricCamera.panY);
    object->setProperty ("isometricCamera", juce::var (iso));

    auto serialisePointArray = [] (const auto& points)
    {
        juce::Array<juce::var> array;
        for (const auto& point : points)
        {
            auto* pt = new juce::DynamicObject();
            pt->setProperty ("x", point.x);
            pt->setProperty ("y", point.y);
            array.add (juce::var (pt));
        }
        return juce::var (array);
    };

    auto serialiseSnakeArray = [&]()
    {
        juce::Array<juce::var> array;
        for (const auto& snake : performanceSnakes)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("body", serialisePointArray (snake.body));
            item->setProperty ("dirX", snake.direction.x);
            item->setProperty ("dirY", snake.direction.y);
            item->setProperty ("colour", snake.colour.toDisplayString (true));
            item->setProperty ("clockwise", snake.clockwise);
            item->setProperty ("orbitIndex", snake.orbitIndex);
            array.add (juce::var (item));
        }
        return juce::var (array);
    };

    auto serialiseDiscArray = [&]()
    {
        juce::Array<juce::var> array;
        for (const auto& disc : performanceDiscs)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("x", disc.cell.x);
            item->setProperty ("y", disc.cell.y);
            item->setProperty ("dirX", disc.direction.x);
            item->setProperty ("dirY", disc.direction.y);
            array.add (juce::var (item));
        }
        return juce::var (array);
    };

    auto serialiseTrackArray = [&]()
    {
        juce::Array<juce::var> array;
        for (const auto& track : performanceTracks)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("x", track.cell.x);
            item->setProperty ("y", track.cell.y);
            item->setProperty ("horizontal", track.horizontal);
            array.add (juce::var (item));
        }
        return juce::var (array);
    };

    auto serialiseRippleArray = [&]()
    {
        juce::Array<juce::var> array;
        for (const auto& ripple : performanceRipples)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("x", ripple.centre.x);
            item->setProperty ("y", ripple.centre.y);
            item->setProperty ("radius", ripple.radius);
            item->setProperty ("maxRadius", ripple.maxRadius);
            item->setProperty ("colour", ripple.colour.toDisplayString (true));
            array.add (juce::var (item));
        }
        return juce::var (array);
    };

    auto serialiseSequencerArray = [&]()
    {
        juce::Array<juce::var> array;
        for (const auto& seq : performanceSequencers)
        {
            auto* item = new juce::DynamicObject();
            item->setProperty ("x", seq.cell.x);
            item->setProperty ("y", seq.cell.y);
            item->setProperty ("dirX", seq.direction.x);
            item->setProperty ("dirY", seq.direction.y);
            item->setProperty ("prevX", seq.previousCell.x);
            item->setProperty ("prevY", seq.previousCell.y);
            item->setProperty ("hasPreviousCell", seq.hasPreviousCell);
            item->setProperty ("colour", seq.colour.toDisplayString (true));
            array.add (juce::var (item));
        }
        return juce::var (array);
    };

    auto* tetris = new juce::DynamicObject();
    tetris->setProperty ("type", static_cast<int> (tetrisPiece.type));
    tetris->setProperty ("rotation", tetrisPiece.rotation);
    tetris->setProperty ("anchorX", tetrisPiece.anchor.x);
    tetris->setProperty ("anchorY", tetrisPiece.anchor.y);
    tetris->setProperty ("z", tetrisPiece.z);
    tetris->setProperty ("active", tetrisPiece.active);
    tetris->setProperty ("nextType", static_cast<int> (nextTetrisType));
    object->setProperty ("tetrisPiece", juce::var (tetris));

    if (automataHoverCell.has_value())
    {
        auto* hover = new juce::DynamicObject();
        hover->setProperty ("x", automataHoverCell->x);
        hover->setProperty ("y", automataHoverCell->y);
        object->setProperty ("automataHoverCell", juce::var (hover));
    }

    object->setProperty ("performanceOrbitCenters", serialisePointArray (performanceOrbitCenters));
    object->setProperty ("performanceAutomataCells", serialisePointArray (performanceAutomataCells));
    object->setProperty ("performanceSnakes", serialiseSnakeArray());
    object->setProperty ("performanceDiscs", serialiseDiscArray());
    object->setProperty ("performanceTracks", serialiseTrackArray());
    object->setProperty ("performanceRipples", serialiseRippleArray());
    object->setProperty ("performanceSequencers", serialiseSequencerArray());

    return juce::var (object);
}

void GameComponent::restoreVoyageSession (const juce::var& sessionState)
{
    auto* object = sessionState.getDynamicObject();
    if (object == nullptr)
        return;

    selectedSystemIndex = juce::jlimit (0, juce::jmax (0, galaxy.systems.size() - 1),
                                        static_cast<int> (object->getProperty ("selectedSystemIndex")));
    selectedPlanetIndex = 0;
    if (galaxy.systems.size() > 0)
    {
        const auto& system = *galaxy.systems.getUnchecked (selectedSystemIndex);
        selectedPlanetIndex = juce::jlimit (0, juce::jmax (0, system.planets.size() - 1),
                                            static_cast<int> (object->getProperty ("selectedPlanetIndex")));
    }

    titleResumeAvailable = static_cast<bool> (object->getProperty ("titleResumeAvailable"));
    resumeScene = static_cast<Scene> (juce::jlimit (0, 3, static_cast<int> (object->getProperty ("resumeScene"))));
    builderViewMode = static_cast<BuilderViewMode> (juce::jlimit (0, 1, static_cast<int> (object->getProperty ("builderViewMode"))));
    topDownBuildMode = static_cast<TopDownBuildMode> (juce::jlimit (0, 2, static_cast<int> (object->getProperty ("topDownBuildMode"))));
    builderCursorX = static_cast<int> (object->getProperty ("builderCursorX"));
    builderCursorY = static_cast<int> (object->getProperty ("builderCursorY"));
    builderLayer = static_cast<int> (object->getProperty ("builderLayer"));
    isometricPlacementHeight = static_cast<int> (object->getProperty ("isometricPlacementHeight"));
    isometricChordType = static_cast<IsometricChordType> (juce::jlimit (0, 7, static_cast<int> (object->getProperty ("isometricChordType"))));
    firstPersonPlacementOffset = static_cast<int> (object->getProperty ("firstPersonPlacementOffset"));
    performanceMode = static_cast<bool> (object->getProperty ("performanceMode"));
    performanceEntryView = static_cast<BuilderViewMode> (juce::jlimit (0, 1, static_cast<int> (object->getProperty ("performanceEntryView"))));
    performanceEntryTopDownMode = static_cast<TopDownBuildMode> (juce::jlimit (0, 2, static_cast<int> (object->getProperty ("performanceEntryTopDownMode"))));
    tetrisBuildLayer = static_cast<int> (object->getProperty ("tetrisBuildLayer"));
    automataBuildLayer = static_cast<int> (object->getProperty ("automataBuildLayer"));
    performanceRegionMode = static_cast<int> (object->getProperty ("performanceRegionMode"));
    performanceAgentCount = static_cast<int> (object->getProperty ("performanceAgentCount"));
    performanceAgentMode = static_cast<PerformanceAgentMode> (juce::jlimit (0, 6, static_cast<int> (object->getProperty ("performanceAgentMode"))));
    performancePlacementMode = static_cast<PerformancePlacementMode> (juce::jlimit (0, 2, static_cast<int> (object->getProperty ("performancePlacementMode"))));
    performanceTrackHorizontal = static_cast<bool> (object->getProperty ("performanceTrackHorizontal"));
    performanceSelectedDirection = {
        static_cast<int> (object->getProperty ("performanceSelectedDirectionX")),
        static_cast<int> (object->getProperty ("performanceSelectedDirectionY"))
    };
    performanceTick = static_cast<int> (object->getProperty ("performanceTick"));
    performanceTenoriColumn = static_cast<int> (object->getProperty ("performanceTenoriColumn"));
    performanceBpm = static_cast<double> (object->getProperty ("performanceBpm"));
    performanceBeatMuted = static_cast<bool> (object->getProperty ("performanceBeatMuted"));
    snakeTriggerMode = static_cast<SnakeTriggerMode> (juce::jlimit (0, 1, static_cast<int> (object->getProperty ("snakeTriggerMode"))));
    synthEngine = static_cast<SynthEngine> (juce::jlimit (0, 5, static_cast<int> (object->getProperty ("synthEngine"))));
    drumMode = static_cast<DrumMode> (juce::jlimit (0, 4, static_cast<int> (object->getProperty ("drumMode"))));
    scaleType = static_cast<ScaleType> (juce::jlimit (0, 4, static_cast<int> (object->getProperty ("scaleType"))));
    performanceKeyRoot = static_cast<int> (object->getProperty ("performanceKeyRoot"));
    performanceImprovCounter = static_cast<int> (object->getProperty ("performanceImprovCounter"));
    performanceLastImprovMidi = static_cast<int> (object->getProperty ("performanceLastImprovMidi"));
    performanceSynthIndex = static_cast<int> (synthEngine);
    performanceDrumIndex = static_cast<int> (drumMode);
    performanceScaleIndex = static_cast<int> (scaleType);

    if (const auto* fp = object->getProperty ("firstPersonState").getDynamicObject(); fp != nullptr)
    {
        firstPersonState.x = static_cast<float> (double (fp->getProperty ("x")));
        firstPersonState.y = static_cast<float> (double (fp->getProperty ("y")));
        firstPersonState.eyeZ = static_cast<float> (double (fp->getProperty ("eyeZ")));
        firstPersonState.verticalVelocity = static_cast<float> (double (fp->getProperty ("verticalVelocity")));
        firstPersonState.yaw = static_cast<float> (double (fp->getProperty ("yaw")));
        firstPersonState.pitch = static_cast<float> (double (fp->getProperty ("pitch")));
    }

    if (const auto* iso = object->getProperty ("isometricCamera").getDynamicObject(); iso != nullptr)
    {
        isometricCamera.rotation = static_cast<int> (iso->getProperty ("rotation"));
        isometricCamera.zoom = static_cast<float> (double (iso->getProperty ("zoom")));
        isometricCamera.heightScale = static_cast<float> (double (iso->getProperty ("heightScale")));
        isometricCamera.panX = static_cast<float> (double (iso->getProperty ("panX")));
        isometricCamera.panY = static_cast<float> (double (iso->getProperty ("panY")));
    }

    auto deserialisePointArray = [] (const juce::var& value)
    {
        std::vector<juce::Point<int>> points;
        if (const auto* array = value.getArray(); array != nullptr)
        {
            points.reserve (static_cast<size_t> (array->size()));
            for (const auto& item : *array)
            {
                if (const auto* point = item.getDynamicObject(); point != nullptr)
                    points.emplace_back (static_cast<int> (point->getProperty ("x")),
                                         static_cast<int> (point->getProperty ("y")));
            }
        }
        return points;
    };

    if (const auto* tetris = object->getProperty ("tetrisPiece").getDynamicObject(); tetris != nullptr)
    {
        tetrisPiece.type = static_cast<TetrominoType> (juce::jlimit (0, 6, static_cast<int> (tetris->getProperty ("type"))));
        tetrisPiece.rotation = static_cast<int> (tetris->getProperty ("rotation"));
        tetrisPiece.anchor = { static_cast<int> (tetris->getProperty ("anchorX")), static_cast<int> (tetris->getProperty ("anchorY")) };
        tetrisPiece.z = static_cast<int> (tetris->getProperty ("z"));
        tetrisPiece.active = static_cast<bool> (tetris->getProperty ("active"));
        nextTetrisType = static_cast<TetrominoType> (juce::jlimit (0, 6, static_cast<int> (tetris->getProperty ("nextType"))));
    }

    automataHoverCell.reset();
    if (const auto* hover = object->getProperty ("automataHoverCell").getDynamicObject(); hover != nullptr)
        automataHoverCell = juce::Point<int> (static_cast<int> (hover->getProperty ("x")), static_cast<int> (hover->getProperty ("y")));

    performanceOrbitCenters = deserialisePointArray (object->getProperty ("performanceOrbitCenters"));
    performanceAutomataCells = deserialisePointArray (object->getProperty ("performanceAutomataCells"));
    performanceDiscs.clear();
    if (const auto* discs = object->getProperty ("performanceDiscs").getArray(); discs != nullptr)
        for (const auto& item : *discs)
            if (const auto* disc = item.getDynamicObject(); disc != nullptr)
                performanceDiscs.push_back ({
                    { static_cast<int> (disc->getProperty ("x")), static_cast<int> (disc->getProperty ("y")) },
                    { static_cast<int> (disc->getProperty ("dirX")), static_cast<int> (disc->getProperty ("dirY")) }
                });

    performanceTracks.clear();
    if (const auto* tracks = object->getProperty ("performanceTracks").getArray(); tracks != nullptr)
        for (const auto& item : *tracks)
            if (const auto* track = item.getDynamicObject(); track != nullptr)
                performanceTracks.push_back ({
                    { static_cast<int> (track->getProperty ("x")), static_cast<int> (track->getProperty ("y")) },
                    static_cast<bool> (track->getProperty ("horizontal"))
                });

    performanceRipples.clear();
    if (const auto* ripples = object->getProperty ("performanceRipples").getArray(); ripples != nullptr)
        for (const auto& item : *ripples)
            if (const auto* ripple = item.getDynamicObject(); ripple != nullptr)
                performanceRipples.push_back ({
                    { static_cast<int> (ripple->getProperty ("x")), static_cast<int> (ripple->getProperty ("y")) },
                    static_cast<int> (ripple->getProperty ("radius")),
                    static_cast<int> (ripple->getProperty ("maxRadius")),
                    juce::Colour::fromString (ripple->getProperty ("colour").toString())
                });

    performanceSequencers.clear();
    if (const auto* sequencers = object->getProperty ("performanceSequencers").getArray(); sequencers != nullptr)
        for (const auto& item : *sequencers)
            if (const auto* seq = item.getDynamicObject(); seq != nullptr)
                performanceSequencers.push_back ({
                    { static_cast<int> (seq->getProperty ("x")), static_cast<int> (seq->getProperty ("y")) },
                    { static_cast<int> (seq->getProperty ("dirX")), static_cast<int> (seq->getProperty ("dirY")) },
                    { static_cast<int> (seq->getProperty ("prevX")), static_cast<int> (seq->getProperty ("prevY")) },
                    juce::Colour::fromString (seq->getProperty ("colour").toString()),
                    static_cast<bool> (seq->getProperty ("hasPreviousCell"))
                });

    performanceSnakes.clear();
    if (const auto* snakes = object->getProperty ("performanceSnakes").getArray(); snakes != nullptr)
    {
        for (const auto& item : *snakes)
        {
            const auto* snake = item.getDynamicObject();
            if (snake == nullptr)
                continue;

            PerformanceSnake restored;
            restored.body = deserialisePointArray (snake->getProperty ("body"));
            restored.direction = { static_cast<int> (snake->getProperty ("dirX")), static_cast<int> (snake->getProperty ("dirY")) };
            restored.colour = juce::Colour::fromString (snake->getProperty ("colour").toString());
            restored.clockwise = static_cast<bool> (snake->getProperty ("clockwise"));
            restored.orbitIndex = static_cast<int> (snake->getProperty ("orbitIndex"));
            performanceSnakes.push_back (std::move (restored));
        }
    }

    activePlanetState.reset();
    const auto requestedScene = static_cast<Scene> (juce::jlimit (0, 3, static_cast<int> (object->getProperty ("currentScene"))));
    const auto restoredScene = requestedScene == Scene::title ? resumeScene : requestedScene;

    if (restoredScene == Scene::landing || restoredScene == Scene::builder)
        ensureActivePlanetLoaded();

    builderCursorX = juce::jlimit (0, juce::jmax (0, getSurfaceWidth() - 1), builderCursorX);
    builderCursorY = juce::jlimit (0, juce::jmax (0, getSurfaceDepth() - 1), builderCursorY);
    builderLayer = juce::jlimit (1, juce::jmax (1, getSurfaceHeight() - 1), builderLayer);
    isometricPlacementHeight = juce::jlimit (1, 4, isometricPlacementHeight);
    firstPersonPlacementOffset = juce::jlimit (1, juce::jmax (1, getSurfaceHeight() - 1), firstPersonPlacementOffset);
    tetrisBuildLayer = juce::jlimit (1, juce::jmax (1, getSurfaceHeight() - 1), tetrisBuildLayer);
    automataBuildLayer = juce::jlimit (1, juce::jmax (1, getSurfaceHeight() - 1), automataBuildLayer);
    performanceTenoriColumn = juce::jlimit (0, juce::jmax (0, getSurfaceWidth() - 1), performanceTenoriColumn);
    performanceAgentCount = juce::jlimit (0, 8, performanceAgentCount);
    performanceKeyRoot = juce::jlimit (0, 11, performanceKeyRoot);
    performanceRegionMode = juce::jlimit (0, 2, performanceRegionMode);
    performanceBpm = juce::jlimit (60.0, 220.0, performanceBpm > 0.0 ? performanceBpm : 120.0);

    setScene (restoredScene);
    updateFirstPersonMouseCapture();
    repaint();
}

void GameComponent::refreshTitleSaveSlots()
{
    titleSaveSlots = persistence.getSaveSlots();
    hasTitleRecoverySlot = persistence.getRecoverySlotSummary (titleRecoverySlot);
    if (titleSaveSlots.empty())
        titleSelectedSlotIndex = -1;
    else if (! juce::isPositiveAndBelow (titleSelectedSlotIndex, static_cast<int> (titleSaveSlots.size())))
        titleSelectedSlotIndex = 0;
}

void GameComponent::queueAutosave()
{
    if (! titleResumeAvailable || galaxy.systems.isEmpty())
        return;

    autosavePending = true;
    autosaveCountdownFrames = 45;
    refreshTitleSaveSlots();
}

void GameComponent::performAutosave()
{
    if (! titleResumeAvailable || galaxy.systems.isEmpty())
        return;

    saveActivePlanet();
    if (persistence.saveRecoveryVoyage (galaxy, serialiseVoyageSession()))
    {
        autosavePending = false;
        autosaveCountdownFrames = 0;
        hasTitleRecoverySlot = persistence.getRecoverySlotSummary (titleRecoverySlot);
    }
}

void GameComponent::openTitleSlotOverlay (TitleSlotOverlayMode mode)
{
    titleSlotOverlayMode = mode;
    refreshTitleSaveSlots();
    titleSlotStatusMessage.clear();
    titleArmedOverwriteSlotKey.clear();
    titleArmedDeleteSlotKey.clear();
    if (mode == TitleSlotOverlayMode::save)
    {
        titleSlotNameDraft = titleResumeAvailable ? galaxy.name : "Voyage";
        titleSelectedSlotIndex = -1;
    }
    repaint();
}

void GameComponent::closeTitleSlotOverlay()
{
    titleSlotOverlayMode = TitleSlotOverlayMode::none;
    titleSelectedSlotIndex = -1;
    titleSlotStatusMessage.clear();
    titleArmedOverwriteSlotKey.clear();
    titleArmedDeleteSlotKey.clear();
}

bool GameComponent::performTitleSaveToSlot (juce::String slotName)
{
    slotName = slotName.trim();
    if (slotName.isEmpty())
    {
        titleSlotStatusMessage = "Enter a slot name first.";
        repaint();
        return false;
    }

    const auto targetSlotKey = getSlotKeyForDraftName (slotName);
    const bool slotExists = std::any_of (titleSaveSlots.begin(), titleSaveSlots.end(), [&] (const SaveSlotSummary& slot)
    {
        return slot.slotKey == targetSlotKey;
    });
    if (slotExists && titleArmedOverwriteSlotKey != targetSlotKey)
    {
        titleArmedOverwriteSlotKey = targetSlotKey;
        titleArmedDeleteSlotKey.clear();
        titleSlotStatusMessage = "Click SAVE .DRD again to overwrite " + slotName + ".";
        repaint();
        return false;
    }

    saveActivePlanet();
    if (! persistence.saveVoyageSlot (slotName, galaxy, serialiseVoyageSession()))
    {
        titleSlotStatusMessage = "Save failed.";
        repaint();
        return false;
    }

    titleResumeAvailable = true;
    refreshTitleSaveSlots();
    titleSlotNameDraft = slotName;
    closeTitleSlotOverlay();
    titleSlotStatusMessage = "Saved to " + slotName + ".drd";
    repaint();
    return true;
}

bool GameComponent::performTitleLoadFromSlot (int slotIndex)
{
    refreshTitleSaveSlots();
    if (! juce::isPositiveAndBelow (slotIndex, static_cast<int> (titleSaveSlots.size())))
        return false;

    GalaxyMetadata loadedGalaxy;
    juce::var sessionState;
    if (! persistence.loadVoyageSlot (titleSaveSlots[static_cast<size_t> (slotIndex)].slotKey, loadedGalaxy, sessionState))
    {
        titleSlotStatusMessage = "Load failed.";
        repaint();
        return false;
    }

    saveActivePlanet();
    flushPerformanceLogSession();
    resetPerformanceState();
    activePlanetState.reset();
    galaxy = std::move (loadedGalaxy);
    restoreVoyageSession (sessionState);
    hoveredTitleAction = TitleAction::none;
    closeTitleSlotOverlay();
    titleResumeAvailable = true;
    queueAutosave();
    return true;
}

bool GameComponent::performTitleDeleteSlot (int slotIndex)
{
    refreshTitleSaveSlots();
    if (! juce::isPositiveAndBelow (slotIndex, static_cast<int> (titleSaveSlots.size())))
        return false;

    const auto slot = titleSaveSlots[static_cast<size_t> (slotIndex)];
    if (titleArmedDeleteSlotKey != slot.slotKey)
    {
        titleArmedDeleteSlotKey = slot.slotKey;
        titleArmedOverwriteSlotKey.clear();
        titleSlotStatusMessage = "Click DELETE again to remove " + slot.slotName + ".";
        repaint();
        return false;
    }

    if (! persistence.deleteVoyageSlot (slot.slotKey))
    {
        titleSlotStatusMessage = "Delete failed.";
        repaint();
        return false;
    }

    refreshTitleSaveSlots();
    titleSelectedSlotIndex = juce::jlimit (-1, static_cast<int> (titleSaveSlots.size()) - 1, titleSelectedSlotIndex);
    titleArmedDeleteSlotKey.clear();
    titleSlotStatusMessage = "Deleted " + slot.slotName + ".";
    repaint();
    return true;
}

bool GameComponent::performTitleRenameSlot (int slotIndex, juce::String newName)
{
    refreshTitleSaveSlots();
    if (! juce::isPositiveAndBelow (slotIndex, static_cast<int> (titleSaveSlots.size())))
        return false;

    newName = newName.trim();
    if (newName.isEmpty())
    {
        titleSlotStatusMessage = "Enter a slot name first.";
        repaint();
        return false;
    }

    const auto slot = titleSaveSlots[static_cast<size_t> (slotIndex)];
    if (! persistence.renameVoyageSlot (slot.slotKey, newName))
    {
        titleSlotStatusMessage = "Rename failed. That slot name may already exist.";
        repaint();
        return false;
    }

    refreshTitleSaveSlots();
    for (int i = 0; i < static_cast<int> (titleSaveSlots.size()); ++i)
        if (titleSaveSlots[static_cast<size_t> (i)].slotName == newName)
            titleSelectedSlotIndex = i;

    titleArmedDeleteSlotKey.clear();
    titleArmedOverwriteSlotKey.clear();
    titleSlotNameDraft = newName;
    titleSlotStatusMessage = "Renamed slot to " + newName + ".";
    repaint();
    return true;
}

juce::String GameComponent::getSlotKeyForDraftName (juce::String slotName) const
{
    slotName = slotName.trim();
    if (slotName.isEmpty())
        slotName = "Voyage";

    juce::String key;
    for (auto ch : slotName)
    {
        if (juce::CharacterFunctions::isLetterOrDigit (ch))
            key << juce::CharacterFunctions::toLowerCase (ch);
        else if (ch == ' ' || ch == '-' || ch == '_')
            key << '-';
    }

    while (key.contains ("--"))
        key = key.replace ("--", "-");

    key = key.trimCharactersAtStart ("-").trimCharactersAtEnd ("-");
    return key.isEmpty() ? "voyage" : key;
}

juce::Rectangle<float> GameComponent::getTitleSlotOverlayBounds (juce::Rectangle<float> area) const
{
    return area.reduced (area.getWidth() * 0.18f, area.getHeight() * 0.12f);
}

juce::Rectangle<float> GameComponent::getTitleSlotInputBounds (juce::Rectangle<float> overlay) const
{
    auto box = overlay.reduced (28.0f, 24.0f);
    box.removeFromTop (84.0f);
    return box.removeFromTop (56.0f);
}

juce::Rectangle<float> GameComponent::getTitleSlotConfirmBounds (juce::Rectangle<float> overlay) const
{
    auto box = getTitleSlotInputBounds (overlay);
    return box.removeFromRight (170.0f).reduced (0.0f, 8.0f);
}

juce::Rectangle<float> GameComponent::getTitleSlotRenameBounds (juce::Rectangle<float> overlay) const
{
    auto box = getTitleSlotInputBounds (overlay);
    return box.removeFromRight (358.0f).withTrimmedLeft (180.0f).reduced (0.0f, 8.0f);
}

juce::Rectangle<float> GameComponent::getTitleSlotDeleteBounds (juce::Rectangle<float> overlay) const
{
    auto box = getTitleSlotInputBounds (overlay);
    return box.removeFromRight (546.0f).withTrimmedLeft (368.0f).reduced (0.0f, 8.0f);
}

juce::Rectangle<float> GameComponent::getTitleSlotRowBounds (juce::Rectangle<float> overlay, int index) const
{
    auto list = overlay.reduced (28.0f, 24.0f);
    list.removeFromTop (titleSlotOverlayMode == TitleSlotOverlayMode::save ? 152.0f : 84.0f);
    const float gap = 10.0f;
    const float rowHeight = 74.0f;
    list.removeFromTop ((rowHeight + gap) * static_cast<float> (index));
    return list.removeFromTop (rowHeight);
}

void GameComponent::drawHeader (juce::Graphics& g, juce::Rectangle<int> area)
{
    juce::ColourGradient titleGlow (juce::Colour (0x90ffd6a1), static_cast<float> (area.getCentreX()), static_cast<float> (area.getY()),
                                    juce::Colour (0x00ffd6a1), static_cast<float> (area.getCentreX()), static_cast<float> (area.getBottom()), false);
    g.setGradientFill (titleGlow);
    g.fillRect (area);

    g.setColour (warmInk());
    g.setFont (juce::FontOptions (30.0f, juce::Font::bold));
    g.drawText ("KlangKunst Galaxy", area.removeFromTop (34), juce::Justification::centredTop);

    g.setColour (mutedText());
    g.setFont (16.0f);
    g.drawText (getSceneTitle() + "  |  " + getSelectedSystem().name + " / " + getSelectedPlanet().name,
                area, juce::Justification::centred);
}

void GameComponent::drawTitleScene (juce::Graphics& g, juce::Rectangle<int> area)
{
    auto card = titleCardBounds (area.toFloat());

    juce::ColourGradient sky (juce::Colour::fromRGB (14, 28, 92),
                              area.getCentreX(), static_cast<float> (area.getY()),
                              juce::Colour::fromRGB (78, 20, 76),
                              area.getCentreX(), static_cast<float> (area.getBottom()),
                              false);
    sky.addColour (0.28, juce::Colour::fromRGB (28, 82, 172));
    sky.addColour (0.58, juce::Colour::fromRGB (138, 42, 112));
    sky.addColour (0.82, juce::Colour::fromRGB (255, 122, 94));
    g.setGradientFill (sky);
    g.fillRoundedRectangle (card.expanded (30.0f, 24.0f), 44.0f);
    juce::ColourGradient skyGloss (juce::Colour::fromRGBA (198, 236, 255, 118),
                                   card.getCentreX(), card.getY() - 12.0f,
                                    juce::Colour::fromRGBA (255, 255, 255, 0),
                                    card.getCentreX(), card.getY() + 150.0f,
                                    false);
    g.setGradientFill (skyGloss);
    g.fillRoundedRectangle (card.expanded (30.0f, 24.0f).withHeight (174.0f), 44.0f);

    auto drawNebula = [&] (juce::Point<float> centre, float cloudScale, juce::Colour tint)
    {
        g.setColour (tint.withAlpha (0.20f));
        g.fillEllipse (juce::Rectangle<float> (92.0f * cloudScale, 56.0f * cloudScale).withCentre ({ centre.x - 26.0f * cloudScale, centre.y }));
        g.fillEllipse (juce::Rectangle<float> (126.0f * cloudScale, 74.0f * cloudScale).withCentre (centre));
        g.fillEllipse (juce::Rectangle<float> (88.0f * cloudScale, 48.0f * cloudScale).withCentre ({ centre.x + 38.0f * cloudScale, centre.y + 5.0f * cloudScale }));
    };
    drawNebula ({ card.getX() + 160.0f, card.getY() + 76.0f }, 1.0f, juce::Colour::fromRGB (92, 208, 255));
    drawNebula ({ card.getRight() - 190.0f, card.getY() + 104.0f }, 0.96f, juce::Colour::fromRGB (255, 110, 198));
    drawNebula ({ card.getCentreX() + 40.0f, card.getY() + 44.0f }, 0.84f, juce::Colour::fromRGB (255, 174, 82));

    fillGlow (g, juce::Rectangle<float> (240.0f, 240.0f).withCentre ({ card.getRight() - 140.0f, card.getY() + 126.0f }),
              juce::Colour::fromRGB (255, 196, 92), 0.24f);
    g.setColour (juce::Colour::fromRGBA (255, 208, 96, 196));
    g.fillEllipse (juce::Rectangle<float> (166.0f, 166.0f).withCentre ({ card.getRight() - 140.0f, card.getY() + 120.0f }));
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 44));
    g.fillEllipse (juce::Rectangle<float> (70.0f, 70.0f).withCentre ({ card.getRight() - 168.0f, card.getY() + 92.0f }));

    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 28));
    g.fillRoundedRectangle (card.expanded (24.0f, 18.0f), 40.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 72));
    g.fillRoundedRectangle (card.expanded (10.0f, 8.0f).withHeight (32.0f).translated (0.0f, 6.0f), 18.0f);

    juce::ColourGradient fill (juce::Colour::fromRGBA (18, 30, 84, 232),
                               card.getCentreX(), card.getY(),
                               juce::Colour::fromRGBA (20, 12, 40, 242),
                               card.getCentreX(), card.getBottom(),
                               false);
    fill.addColour (0.24, juce::Colour::fromRGBA (28, 88, 164, 228));
    fill.addColour (0.56, juce::Colour::fromRGBA (82, 34, 122, 224));
    fill.addColour (0.80, juce::Colour::fromRGBA (52, 18, 74, 220));
    g.setGradientFill (fill);
    g.fillRoundedRectangle (card, 32.0f);
    g.setColour (juce::Colour::fromRGBA (112, 214, 255, 170));
    g.drawRoundedRectangle (card, 32.0f, 2.4f);
    juce::ColourGradient cardGloss (juce::Colour::fromRGBA (255, 255, 255, 46),
                                    card.getCentreX(), card.getY() + 6.0f,
                                    juce::Colour::fromRGBA (255, 255, 255, 0),
                                    card.getCentreX(), card.getY() + 190.0f,
                                    false);
    g.setGradientFill (cardGloss);
    g.fillRoundedRectangle (card.withHeight (198.0f).reduced (8.0f, 8.0f), 24.0f);
    juce::Path diagonalSheen;
    diagonalSheen.startNewSubPath (card.getX() + 34.0f, card.getY() + 34.0f);
    diagonalSheen.lineTo (card.getX() + 240.0f, card.getY() + 34.0f);
    diagonalSheen.lineTo (card.getX() + 84.0f, card.getY() + 214.0f);
    diagonalSheen.lineTo (card.getX() + 12.0f, card.getY() + 214.0f);
    diagonalSheen.closeSubPath();
    g.setColour (juce::Colour::fromRGBA (88, 122, 182, 8));
    g.fillPath (diagonalSheen);

    auto inner = card.reduced (42.0f, 32.0f);
    auto topRow = inner.removeFromTop (176.0f);
    inner.removeFromTop (18.0f);
    auto actionsRow = inner.removeFromTop (116.0f);
    inner.removeFromTop (14.0f);
    auto hintArea = inner.removeFromTop (28.0f);

    auto leftHero = topRow.removeFromLeft (topRow.getWidth() * 0.55f);
    topRow.removeFromLeft (26.0f);
    auto rightStory = topRow;

    auto titleArea = leftHero.removeFromTop (98.0f);
    leftHero.removeFromTop (8.0f);
    auto subtitleArea = leftHero.removeFromTop (60.0f);
    leftHero.removeFromTop (12.0f);

    fillGlow (g, juce::Rectangle<float> (titleArea.getWidth() + 80.0f, titleArea.getHeight() + 36.0f)
                  .withCentre ({ titleArea.getCentreX() + 14.0f, titleArea.getCentreY() + 4.0f }),
              juce::Colour::fromRGB (255, 166, 112), 0.12f);
    auto titleShadow = titleArea.translated (0.0f, 5.0f);
    titleArea.removeFromTop (8.0f);
    auto titleShadowArea = titleArea.translated (0.0f, 6.0f);
    auto titleMain = titleArea.removeFromTop (34.0f);
    titleArea.removeFromTop (2.0f);
    auto titleSub = titleArea.removeFromTop (42.0f);
    g.setColour (juce::Colour::fromRGBA (0, 0, 0, 156));
    g.setFont (juce::FontOptions (50.0f, juce::Font::bold));
    g.drawText ("KlangKunst", titleShadowArea.removeFromTop (38.0f).toNearestInt(), juce::Justification::centredLeft);
    g.drawText ("Galaxy", titleShadowArea.removeFromTop (42.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colour::fromRGB (252, 244, 222));
    g.setFont (juce::FontOptions (50.0f, juce::Font::bold));
    g.drawText ("KlangKunst", titleMain.toNearestInt(), juce::Justification::centredLeft);
    juce::ColourGradient galaxyWord (juce::Colour::fromRGB (255, 186, 108),
                                     titleSub.getX(), titleSub.getY(),
                                     juce::Colour::fromRGB (255, 108, 170),
                                     titleSub.getRight(), titleSub.getBottom(),
                                     false);
    g.setGradientFill (galaxyWord);
    g.setFont (juce::FontOptions (58.0f, juce::Font::bold));
    g.drawText ("Galaxy", titleSub.toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 106));
    g.fillRoundedRectangle (titleMain.withY (titleMain.getY() + 8.0f).withHeight (11.0f).withTrimmedRight (titleMain.getWidth() * 0.28f), 5.5f);
    g.setColour (juce::Colour::fromRGBA (255, 192, 116, 182));
    g.fillRoundedRectangle (titleSub.withY (titleSub.getBottom() - 8.0f).withHeight (8.0f).withTrimmedRight (titleSub.getWidth() * 0.42f), 4.0f);

    g.setColour (juce::Colour::fromRGBA (214, 232, 255, 220));
    g.setFont (juce::FontOptions (15.5f));
    g.drawFittedText ("Explore. Build. Play.",
                      subtitleArea.toNearestInt(),
                      juce::Justification::topLeft,
                      3);

    juce::Path motifLine;
    const float motifY = subtitleArea.getY() + 10.0f;
    motifLine.startNewSubPath (subtitleArea.getRight() - 10.0f, motifY);
    motifLine.lineTo (subtitleArea.getRight() + 170.0f, motifY);
    g.setColour (juce::Colour::fromRGBA (160, 220, 255, 64));
    g.strokePath (motifLine, juce::PathStrokeType (1.2f));
    for (int i = 0; i < 3; ++i)
    {
        const auto x = subtitleArea.getRight() + 34.0f + static_cast<float> (i) * 78.0f;
        const auto size = (i == 1) ? 14.0f : 10.0f;
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, static_cast<juce::uint8> (96 - i * 12)));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ x, motifY }));
    }

    g.setColour (juce::Colour::fromRGBA (24, 28, 74, 212));
    g.fillRoundedRectangle (rightStory, 22.0f);
    g.setColour (juce::Colour::fromRGBA (126, 224, 255, 168));
    g.drawRoundedRectangle (rightStory, 22.0f, 1.8f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
    g.fillRoundedRectangle (rightStory.withHeight (18.0f).reduced (10.0f, 2.0f), 10.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 16));
    g.fillRoundedRectangle (rightStory.reduced (10.0f, 12.0f).withHeight (44.0f), 18.0f);

    auto storyInner = rightStory.reduced (18.0f, 18.0f);
    auto boardArea = storyInner.removeFromTop (78.0f);
    storyInner.removeFromTop (12.0f);
    auto stepsArea = storyInner;

    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 44));
    g.fillEllipse (boardArea.expanded (0.0f, 18.0f));
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
    g.fillEllipse (boardArea.expanded (-20.0f, 0.0f).translated (0.0f, -4.0f));

    auto boardRect = boardArea.withSizeKeepingCentre (186.0f, 96.0f);
    juce::Path isoBoard;
    const auto a = juce::Point<float> (boardRect.getCentreX(), boardRect.getY());
    const auto b = juce::Point<float> (boardRect.getRight(), boardRect.getCentreY());
    const auto c = juce::Point<float> (boardRect.getCentreX(), boardRect.getBottom());
    const auto d = juce::Point<float> (boardRect.getX(), boardRect.getCentreY());
    isoBoard.startNewSubPath (a);
    isoBoard.lineTo (b);
    isoBoard.lineTo (c);
    isoBoard.lineTo (d);
    isoBoard.closeSubPath();
    g.setColour (juce::Colour::fromRGBA (34, 72, 156, 228));
    g.fillPath (isoBoard);
    g.setColour (juce::Colour::fromRGBA (140, 226, 255, 144));
    g.strokePath (isoBoard, juce::PathStrokeType (2.0f));
    juce::Path boardSheen;
    boardSheen.startNewSubPath ({ boardRect.getCentreX(), boardRect.getY() + 6.0f });
    boardSheen.lineTo ({ boardRect.getCentreX() + boardRect.getWidth() * 0.24f, boardRect.getCentreY() - 2.0f });
    boardSheen.lineTo ({ boardRect.getCentreX() - boardRect.getWidth() * 0.10f, boardRect.getCentreY() - 2.0f });
    boardSheen.lineTo ({ boardRect.getCentreX() - boardRect.getWidth() * 0.24f, boardRect.getY() + 18.0f });
    boardSheen.closeSubPath();
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 58));
    g.fillPath (boardSheen);
    juce::Path boardGloss;
    boardGloss.startNewSubPath (boardRect.getCentreX() - boardRect.getWidth() * 0.20f, boardRect.getY() + 16.0f);
    boardGloss.lineTo (boardRect.getCentreX() + boardRect.getWidth() * 0.04f, boardRect.getY() + 16.0f);
    boardGloss.lineTo (boardRect.getCentreX() - boardRect.getWidth() * 0.10f, boardRect.getCentreY() - 4.0f);
    boardGloss.lineTo (boardRect.getCentreX() - boardRect.getWidth() * 0.28f, boardRect.getCentreY() - 4.0f);
    boardGloss.closeSubPath();
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 72));
    g.fillPath (boardGloss);
    for (int i = 1; i <= 3; ++i)
    {
        const float t = static_cast<float> (i) / 4.0f;
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 36));
        g.drawLine (juce::Line<float> ({ juce::jmap (t, d.x, a.x), juce::jmap (t, d.y, a.y) },
                                       { juce::jmap (t, c.x, b.x), juce::jmap (t, c.y, b.y) }),
                    1.0f);
        g.drawLine (juce::Line<float> ({ juce::jmap (t, a.x, b.x), juce::jmap (t, a.y, b.y) },
                                       { juce::jmap (t, d.x, c.x), juce::jmap (t, d.y, c.y) }),
                    1.0f);
    }

    auto drawMiniBlock = [&] (juce::Point<float> centre, juce::Colour colour)
    {
        const float w = 24.0f;
        const float h = 12.0f;
        const float rise = 22.0f;
        juce::Path top;
        top.startNewSubPath (centre.x, centre.y - rise);
        top.lineTo (centre.x + w * 0.5f, centre.y - rise + h * 0.5f);
        top.lineTo (centre.x, centre.y - rise + h);
        top.lineTo (centre.x - w * 0.5f, centre.y - rise + h * 0.5f);
        top.closeSubPath();
        juce::Path left;
        left.startNewSubPath (centre.x - w * 0.5f, centre.y - rise + h * 0.5f);
        left.lineTo (centre.x, centre.y - rise + h);
        left.lineTo (centre.x, centre.y);
        left.lineTo (centre.x - w * 0.5f, centre.y - h * 0.5f);
        left.closeSubPath();
        juce::Path right;
        right.startNewSubPath (centre.x + w * 0.5f, centre.y - rise + h * 0.5f);
        right.lineTo (centre.x, centre.y - rise + h);
        right.lineTo (centre.x, centre.y);
        right.lineTo (centre.x + w * 0.5f, centre.y - h * 0.5f);
        right.closeSubPath();
        g.setColour (colour.withMultipliedBrightness (1.12f));
        g.fillPath (top);
        g.setColour (colour.withMultipliedBrightness (0.86f));
        g.fillPath (left);
        g.setColour (colour.withMultipliedBrightness (0.72f));
        g.fillPath (right);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 42));
        g.strokePath (top, juce::PathStrokeType (1.0f));
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 72));
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ centre.x - 3.0f, centre.y - rise + 7.0f }));
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 88));
        g.fillRoundedRectangle (juce::Rectangle<float> (w * 0.28f, 4.0f).withCentre ({ centre.x - 2.0f, centre.y - rise + 8.0f }), 2.0f);
    };
    drawMiniBlock ({ boardRect.getCentreX() - 42.0f, boardRect.getCentreY() + 24.0f }, juce::Colour::fromRGB (255, 214, 84));
    drawMiniBlock ({ boardRect.getCentreX() - 8.0f, boardRect.getCentreY() + 4.0f }, juce::Colour::fromRGB (255, 96, 92));
    drawMiniBlock ({ boardRect.getCentreX() + 28.0f, boardRect.getCentreY() + 20.0f }, juce::Colour::fromRGB (116, 224, 110));
    drawMiniBlock ({ boardRect.getCentreX() + 58.0f, boardRect.getCentreY() - 8.0f }, juce::Colour::fromRGB (92, 198, 255));

    auto legendArea = juce::Rectangle<float> (boardArea.getX() + 12.0f, boardArea.getBottom() + 8.0f, boardArea.getWidth() - 24.0f, 54.0f);
    const float legendGap = 8.0f;
    const float legendRowHeight = (legendArea.getHeight() - legendGap * 2.0f) / 3.0f;
    const std::array<juce::Colour, 3> legendColours {
        juce::Colour::fromRGB (255, 120, 168),
        juce::Colour::fromRGB (255, 196, 92),
        juce::Colour::fromRGB (92, 214, 255)
    };
    for (int i = 0; i < 3; ++i)
    {
        auto row = legendArea.removeFromTop (legendRowHeight);
        if (i < 2)
            legendArea.removeFromTop (legendGap);

        g.setColour (juce::Colour::fromRGBA (18, 24, 58, 188));
        g.fillRoundedRectangle (row, row.getHeight() * 0.5f);
        g.setColour (juce::Colour::fromRGBA (112, 214, 255, 110));
        g.drawRoundedRectangle (row, row.getHeight() * 0.5f, 1.0f);

        auto badge = row.removeFromLeft (28.0f).reduced (2.0f);
        g.setColour (legendColours[static_cast<size_t> (i)].withAlpha (0.92f));
        g.fillEllipse (badge);
        g.setColour (juce::Colours::white.withAlpha (0.84f));
        g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        g.drawText (juce::String (i + 1), badge.toNearestInt(), juce::Justification::centred);

        row.removeFromLeft (8.0f);
        auto barBounds = row.reduced (0.0f, 3.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 12));
        g.fillRoundedRectangle (barBounds.withHeight (8.0f), 4.0f);
        const auto fillWidth = barBounds.getWidth() * (0.18f + 0.22f * static_cast<float> (i));
        g.setColour (legendColours[static_cast<size_t> (i)].withAlpha (0.68f));
        g.fillRoundedRectangle (barBounds.withWidth (fillWidth).withHeight (8.0f), 4.0f);
    }

    auto drawStep = [&] (juce::Rectangle<float> bounds, const juce::String& num, const juce::String& label, const juce::String& desc)
    {
        g.setColour (juce::Colour::fromRGBA (18, 26, 64, 224));
        g.fillRoundedRectangle (bounds, 16.0f);
        g.setColour (juce::Colour::fromRGBA (114, 214, 255, 112));
        g.drawRoundedRectangle (bounds, 16.0f, 1.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
        g.fillRoundedRectangle (bounds.reduced (8.0f, 6.0f).withHeight (16.0f), 8.0f);
        auto numBubble = bounds.removeFromLeft (44.0f).reduced (4.0f);
        g.setColour (juce::Colour::fromRGBA (255, 124, 124, 220));
        g.fillRoundedRectangle (numBubble, 12.0f);
        g.setColour (juce::Colour::fromRGBA (255, 214, 150, 100));
        g.drawRoundedRectangle (numBubble, 12.0f, 1.2f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 88));
        g.fillRoundedRectangle (numBubble.reduced (5.0f, 4.0f).withHeight (8.0f), 6.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (num, numBubble.toNearestInt(), juce::Justification::centred);
        auto textArea = bounds.reduced (12.0f, 8.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (16.0f));
        g.drawText (label, textArea.removeFromTop (20.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colour::fromRGBA (210, 228, 255, 202));
        g.setFont (juce::FontOptions (13.5f));
        g.drawFittedText (desc, textArea.toNearestInt(), juce::Justification::topLeft, 2);
    };
    const float stepGap = 10.0f;
    const float stepHeight = (stepsArea.getHeight() - stepGap * 2.0f) / 3.0f;
    drawStep (stepsArea.removeFromTop (stepHeight), "1", "Chart the galaxy", "Drift between seeded constellations and pick a system from a living cosmic atlas.");
    stepsArea.removeFromTop (stepGap);
    drawStep (stepsArea.removeFromTop (stepHeight), "2", "Descend to a planet", "Drop from the overture of space into a world that materialises only when you commit to land.");
    stepsArea.removeFromTop (stepGap);
    drawStep (stepsArea.removeFromTop (stepHeight), "3", "Build the world", "Shape the silent surface into a persistent musical landscape and hear it answer back.");

    const float buttonGap = 18.0f;
    const float buttonWidth = (actionsRow.getWidth() - buttonGap * 3.0f) / 4.0f;
    const std::array<TitleAction, 4> actions {
        TitleAction::resumeVoyage,
        TitleAction::newVoyage,
        TitleAction::loadVoyage,
        TitleAction::saveVoyage
    };
    const std::array<juce::String, 4> captions {
        "Return to the current voyage and continue charting the galaxy.",
        "Generate a fresh seeded cosmos and begin a new voyage.",
        "Open a named .drd save slot and restore the full voyage.",
        "Write the current galaxy, planet states, and logs into a named .drd slot."
    };
    for (size_t i = 0; i < actions.size(); ++i)
    {
        auto button = actionsRow.removeFromLeft (buttonWidth);
        if (i + 1 < actions.size())
            actionsRow.removeFromLeft (buttonGap);
        const bool enabled = isTitleActionEnabled (actions[i]);
        const bool hovered = enabled && hoveredTitleAction == actions[i];
        const float pulse = hovered ? (0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.006))) : 0.0f;
        const float lift = hovered ? -4.0f : 0.0f;
        button = button.translated (0.0f, lift);
        g.setColour (juce::Colour::fromRGBA (0, 0, 0, hovered ? 118 : (enabled ? 74 : 52)));
        g.fillRoundedRectangle (button.translated (0.0f, 6.0f), 22.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, hovered ? 38 : (enabled ? 18 : 10)));
        g.fillRoundedRectangle (button.translated (0.0f, 2.0f), 22.0f);
        const auto topColour = [&]() -> juce::Colour
        {
            if (! enabled)
                return juce::Colour::fromRGBA (86, 92, 112, 228);
            if (i == 0)
                return hovered ? juce::Colour::fromRGBA (112, 126, 146, 244)
                               : juce::Colour::fromRGBA (76, 88, 108, 232);
            if (i == 1)
                return hovered ? juce::Colour::fromRGBA (34, 118, 214, 246)
                               : juce::Colour::fromRGBA (24, 70, 154, 234);
            if (i == 2)
                return hovered ? juce::Colour::fromRGBA (186, 86, 220, 246)
                               : juce::Colour::fromRGBA (118, 42, 172, 234);
            return hovered ? juce::Colour::fromRGBA (255, 128, 96, 246)
                           : juce::Colour::fromRGBA (184, 66, 94, 234);
        }();

        const auto bottomColour = [&]() -> juce::Colour
        {
            if (! enabled)
                return juce::Colour::fromRGBA (42, 46, 62, 244);
            if (i == 0)
                return juce::Colour::fromRGBA (28, 34, 48, 246);
            if (i == 1)
                return juce::Colour::fromRGBA (10, 20, 64, 248);
            if (i == 2)
                return juce::Colour::fromRGBA (34, 10, 68, 248);
            return juce::Colour::fromRGBA (58, 14, 48, 248);
        }();
        juce::ColourGradient buttonFill (topColour,
                                         button.getCentreX(), button.getY(),
                                         bottomColour,
                                         button.getCentreX(), button.getBottom(),
                                         false);
        g.setGradientFill (buttonFill);
        g.fillRoundedRectangle (button, 22.0f);
        g.setColour (! enabled ? juce::Colour::fromRGBA (172, 178, 194, 92)
                               : (hovered ? juce::Colour::fromRGBA (255, 222, 150, static_cast<uint8_t> (184 + 34 * pulse))
                                          : juce::Colour::fromRGBA (110, 214, 255, 138)));
        g.drawRoundedRectangle (button, 22.0f, hovered ? 2.6f : 1.4f);
        g.setColour (! enabled ? juce::Colour::fromRGBA (255, 255, 255, 12)
                               : (hovered ? juce::Colour::fromRGBA (255, 176, 112, static_cast<uint8_t> (40 + 28 * pulse))
                                          : juce::Colour::fromRGBA (92, 190, 255, 30)));
        g.fillRoundedRectangle (button.reduced (5.0f, 5.0f), 18.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, hovered ? 78 : (enabled ? 52 : 26)));
        g.fillRoundedRectangle (button.withHeight (18.0f).reduced (12.0f, 4.0f), 10.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, hovered ? 122 : (enabled ? 84 : 40)));
        g.fillRoundedRectangle (button.reduced (16.0f, 12.0f).withHeight (14.0f), 7.0f);
        juce::Path buttonSweep;
        buttonSweep.startNewSubPath (button.getX() + 18.0f, button.getY() + 20.0f);
        buttonSweep.lineTo (button.getX() + button.getWidth() * 0.58f, button.getY() + 20.0f);
        buttonSweep.lineTo (button.getX() + button.getWidth() * 0.38f, button.getY() + button.getHeight() - 20.0f);
        buttonSweep.lineTo (button.getX() + 8.0f, button.getY() + button.getHeight() - 20.0f);
        buttonSweep.closeSubPath();
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, hovered ? 56 : (enabled ? 34 : 16)));
        g.fillPath (buttonSweep);
        auto buttonInner = button.reduced (20.0f, 14.0f);
        auto topStrip = buttonInner.removeFromTop (22.0f);
        auto actionTag = topStrip.removeFromLeft (56.0f);
        g.setColour (! enabled ? juce::Colour::fromRGBA (146, 152, 166, 46)
                               : (hovered ? juce::Colour::fromRGBA (255, 210, 140, 118)
                                          : juce::Colour::fromRGBA (112, 214, 255, 78)));
        g.fillRoundedRectangle (actionTag, 11.0f);
        g.setColour (enabled ? juce::Colours::white : juce::Colours::white.withAlpha (0.52f));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("STEP", actionTag.toNearestInt(), juce::Justification::centred);
        buttonInner.removeFromTop (10.0f);
        g.setColour (enabled ? juce::Colours::white : juce::Colours::white.withAlpha (0.55f));
        g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
        g.drawText (titleActionLabel (actions[i]), buttonInner.removeFromTop (30.0f).toNearestInt(), juce::Justification::centredLeft);
        buttonInner.removeFromTop (4.0f);
        g.setColour (enabled ? juce::Colour::fromRGBA (216, 232, 255, 218)
                             : juce::Colour::fromRGBA (188, 196, 210, 138));
        g.setFont (juce::FontOptions (14.0f));
        g.drawFittedText (captions[i],
                          buttonInner.toNearestInt(), juce::Justification::topLeft, 3);
    }

    auto footerCloud = hintArea.withTrimmedTop (2.0f).reduced (56.0f, 0.0f);
    g.setColour (juce::Colour::fromRGBA (18, 24, 58, 214));
    g.fillRoundedRectangle (footerCloud, 14.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
    g.fillRoundedRectangle (footerCloud.withHeight (10.0f).reduced (14.0f, 1.0f), 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
    g.fillRoundedRectangle (footerCloud.reduced (18.0f, 6.0f).withHeight (8.0f), 6.0f);
    g.setColour (juce::Colour::fromRGBA (216, 232, 255, 220));
    g.setFont (juce::FontOptions (13.0f));
    g.drawText ("Resume returns to your voyage   New starts fresh   Load restores a .drd slot   Save writes the current voyage",
                footerCloud.toNearestInt(),
                juce::Justification::centred);

    if (hasTitleRecoverySlot)
    {
        auto autosaveBand = footerCloud.translated (0.0f, -34.0f).withHeight (24.0f);
        g.setColour (juce::Colour::fromRGBA (18, 26, 58, 182));
        g.fillRoundedRectangle (autosaveBand, 10.0f);
        g.setColour (juce::Colour::fromRGBA (138, 228, 255, 96));
        g.drawRoundedRectangle (autosaveBand, 10.0f, 1.0f);
        g.setColour (juce::Colour::fromRGBA (220, 236, 255, 214));
        g.setFont (juce::FontOptions (12.5f));
        g.drawText ("Last autosave: " + titleRecoverySlot.savedUtc + "   |   " + titleRecoverySlot.galaxyName,
                    autosaveBand.toNearestInt(), juce::Justification::centred);
    }

    if (titleSlotOverlayMode != TitleSlotOverlayMode::none)
    {
        g.setColour (juce::Colour::fromRGBA (4, 8, 20, 168));
        g.fillRoundedRectangle (area.toFloat().reduced (8.0f), 28.0f);

        auto overlay = getTitleSlotOverlayBounds (area.toFloat());
        drawPanel (g, overlay.toNearestInt(), juce::Colour::fromRGBA (116, 224, 255, 220));

        auto inner = overlay.reduced (28.0f, 24.0f);
        auto header = inner.removeFromTop (52.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (24.0f, juce::Font::bold));
        g.drawText (titleSlotOverlayMode == TitleSlotOverlayMode::load ? "LOAD VOYAGE" : "SAVE VOYAGE",
                    header.toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colour::fromRGBA (210, 228, 255, 180));
        g.setFont (juce::FontOptions (13.5f));
        g.drawText (titleSlotOverlayMode == TitleSlotOverlayMode::load
                        ? "Choose a named .drd slot to restore the full voyage."
                        : "Type a slot name or pick an existing .drd slot to overwrite.",
                    inner.removeFromTop (24.0f).toNearestInt(), juce::Justification::centredLeft);
        if (hasTitleRecoverySlot)
        {
            g.setColour (juce::Colour::fromRGBA (220, 236, 255, 200));
            g.setFont (juce::FontOptions (12.5f));
            g.drawText ("Last autosave: " + titleRecoverySlot.savedUtc + "   |   " + titleRecoverySlot.galaxyName,
                        inner.removeFromTop (18.0f).toNearestInt(), juce::Justification::centredLeft);
        }

        if (titleSlotOverlayMode == TitleSlotOverlayMode::save)
        {
            auto inputRow = inner.removeFromTop (62.0f);
            auto inputBounds = inputRow.removeFromLeft (inputRow.getWidth() - 558.0f).reduced (0.0f, 6.0f);
            auto renameBounds = getTitleSlotRenameBounds (overlay);
            auto deleteBounds = getTitleSlotDeleteBounds (overlay);
            auto confirmBounds = getTitleSlotConfirmBounds (overlay);
            g.setColour (juce::Colour::fromRGBA (10, 18, 44, 220));
            g.fillRoundedRectangle (inputBounds, 12.0f);
            g.setColour (juce::Colour::fromRGBA (112, 214, 255, 120));
            g.drawRoundedRectangle (inputBounds, 12.0f, 1.5f);
            g.setColour (juce::Colour::fromRGBA (196, 214, 240, 150));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText ("Slot name", inputBounds.removeFromTop (16.0f).toNearestInt(), juce::Justification::centredLeft);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (titleSlotNameDraft.isEmpty() ? "Voyage" : titleSlotNameDraft,
                        inputBounds.reduced (12.0f, 8.0f).toNearestInt(), juce::Justification::centredLeft);

            const auto drawActionButton = [&] (juce::Rectangle<float> bounds, juce::String label, juce::Colour fill, juce::Colour outline)
            {
                g.setColour (fill);
                g.fillRoundedRectangle (bounds, 12.0f);
                g.setColour (outline);
                g.drawRoundedRectangle (bounds, 12.0f, 1.6f);
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
                g.drawText (label, bounds.toNearestInt(), juce::Justification::centred);
            };

            drawActionButton (renameBounds,
                              titleSelectedSlotIndex >= 0 ? "RENAME" : "RENAME",
                              titleSelectedSlotIndex >= 0 ? juce::Colour::fromRGBA (62, 108, 184, 236)
                                                          : juce::Colour::fromRGBA (62, 78, 112, 178),
                              juce::Colour::fromRGBA (138, 228, 255, titleSelectedSlotIndex >= 0 ? 200 : 80));

            const auto deleteSlotKey = titleSelectedSlotIndex >= 0 ? titleSaveSlots[static_cast<size_t> (titleSelectedSlotIndex)].slotKey : juce::String();
            drawActionButton (deleteBounds,
                              titleArmedDeleteSlotKey == deleteSlotKey && ! deleteSlotKey.isEmpty() ? "CONFIRM DELETE" : "DELETE",
                              titleSelectedSlotIndex >= 0 ? juce::Colour::fromRGBA (140, 44, 62, 232)
                                                          : juce::Colour::fromRGBA (82, 52, 62, 178),
                              juce::Colour::fromRGBA (255, 176, 176, titleSelectedSlotIndex >= 0 ? 184 : 80));

            const auto overwriteKey = getSlotKeyForDraftName (titleSlotNameDraft);
            const bool overwriteArmed = titleArmedOverwriteSlotKey == overwriteKey;
            g.setColour (overwriteArmed ? juce::Colour::fromRGBA (196, 112, 52, 240)
                                        : juce::Colour::fromRGBA (36, 84, 162, 236));
            g.fillRoundedRectangle (confirmBounds, 12.0f);
            g.setColour (overwriteArmed ? juce::Colour::fromRGBA (255, 220, 164, 210)
                                        : juce::Colour::fromRGBA (138, 228, 255, 200));
            g.drawRoundedRectangle (confirmBounds, 12.0f, 1.6f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (17.0f, juce::Font::bold));
            g.drawText (overwriteArmed ? "CONFIRM OVERWRITE" : "SAVE .DRD", confirmBounds.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            inner.removeFromTop (62.0f);
            auto loadBounds = getTitleSlotConfirmBounds (overlay);
            auto deleteBounds = getTitleSlotDeleteBounds (overlay);
            const auto deleteSlotKey = titleSelectedSlotIndex >= 0 ? titleSaveSlots[static_cast<size_t> (titleSelectedSlotIndex)].slotKey : juce::String();
            g.setColour (titleSelectedSlotIndex >= 0 ? juce::Colour::fromRGBA (36, 84, 162, 236)
                                                     : juce::Colour::fromRGBA (62, 78, 112, 178));
            g.fillRoundedRectangle (loadBounds, 12.0f);
            g.setColour (juce::Colour::fromRGBA (138, 228, 255, titleSelectedSlotIndex >= 0 ? 200 : 80));
            g.drawRoundedRectangle (loadBounds, 12.0f, 1.6f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText ("LOAD .DRD", loadBounds.toNearestInt(), juce::Justification::centred);

            g.setColour (titleSelectedSlotIndex >= 0 ? juce::Colour::fromRGBA (140, 44, 62, 232)
                                                     : juce::Colour::fromRGBA (82, 52, 62, 178));
            g.fillRoundedRectangle (deleteBounds, 12.0f);
            g.setColour (juce::Colour::fromRGBA (255, 176, 176, titleSelectedSlotIndex >= 0 ? 184 : 80));
            g.drawRoundedRectangle (deleteBounds, 12.0f, 1.6f);
            g.setColour (juce::Colours::white);
            g.setFont (juce::FontOptions (15.0f, juce::Font::bold));
            g.drawText (titleArmedDeleteSlotKey == deleteSlotKey && ! deleteSlotKey.isEmpty() ? "CONFIRM DELETE" : "DELETE",
                        deleteBounds.toNearestInt(), juce::Justification::centred);
        }

        inner.removeFromTop (10.0f);
        if (titleSaveSlots.empty())
        {
            auto empty = inner.removeFromTop (92.0f);
            g.setColour (juce::Colour::fromRGBA (16, 24, 56, 190));
            g.fillRoundedRectangle (empty, 14.0f);
            g.setColour (juce::Colour::fromRGBA (210, 228, 255, 216));
            g.setFont (juce::FontOptions (17.0f));
            g.drawText (titleSlotOverlayMode == TitleSlotOverlayMode::load
                            ? "No .drd save slots yet."
                            : "No save slots yet. Enter a name above to create one.",
                        empty.toNearestInt(), juce::Justification::centred);
        }
        else
        {
            const float rowGap = 10.0f;
            for (int i = 0; i < static_cast<int> (titleSaveSlots.size()) && i < 6; ++i)
            {
                auto row = getTitleSlotRowBounds (overlay, i);
                const auto& slot = titleSaveSlots[static_cast<size_t> (i)];
                const bool selected = i == titleSelectedSlotIndex;
                g.setColour (selected ? juce::Colour::fromRGBA (255, 192, 120, 82)
                                      : juce::Colour::fromRGBA (10, 18, 44, 192));
                g.fillRoundedRectangle (row, 12.0f);
                g.setColour (selected ? juce::Colour::fromRGBA (255, 216, 144, 214)
                                      : juce::Colour::fromRGBA (112, 214, 255, 110));
                g.drawRoundedRectangle (row, 12.0f, selected ? 1.8f : 1.2f);

                auto text = row.reduced (16.0f, 10.0f);
                auto top = text.removeFromTop (24.0f);
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (18.0f, juce::Font::bold));
                g.drawText (slot.slotName + "   ." + "drd", top.toNearestInt(), juce::Justification::centredLeft);
                g.setColour (juce::Colour::fromRGBA (205, 222, 245, 198));
                g.setFont (juce::FontOptions (13.5f));
                g.drawText (slot.galaxyName + "   systems " + juce::String (slot.systemCount)
                                + "   visited " + juce::String (slot.visitedPlanets)
                                + "   saved " + slot.savedUtc,
                            text.toNearestInt(), juce::Justification::centredLeft);

                if (i + 1 < static_cast<int> (titleSaveSlots.size()))
                    inner.removeFromTop (rowGap);
            }
        }

        if (! titleSlotStatusMessage.isEmpty())
        {
            auto footer = overlay.removeFromBottom (42.0f).reduced (26.0f, 4.0f);
            g.setColour (juce::Colour::fromRGBA (210, 228, 255, 214));
            g.setFont (juce::FontOptions (13.0f));
            g.drawText (titleSlotStatusMessage, footer.toNearestInt(), juce::Justification::centredLeft);
        }
    }
}

void GameComponent::drawGalaxyScene (juce::Graphics& g, juce::Rectangle<int> area)
{
    if (galaxyLogOpen)
    {
        drawGalaxyLogbook (g, area);
        return;
    }

    auto mapArea = area.removeFromLeft (area.proportionOfWidth (0.57f)).reduced (10);
    auto sideArea = area.reduced (10);
    const float uiScale = juce::jlimit (0.82f, 1.0f,
                                        juce::jmin (sideArea.getWidth() / 700.0f,
                                                    sideArea.getHeight() / 860.0f));

    auto drawReferencePanel = [&] (juce::Rectangle<int> bounds)
    {
        drawPanel (g, bounds, juce::Colour::fromRGB (92, 190, 255));
    };

    drawReferencePanel (mapArea);
    drawReferencePanel (sideArea);

    auto mapFloat = mapArea.toFloat();
    const auto& system = getSelectedSystem();
    const auto& selectedPlanet = getSelectedPlanet();
    const auto identityColour = getPlanetIdentityColour (selectedPlanet);
    const auto selectedPoint = juce::Point<float> (static_cast<float> (mapArea.getX()) + system.galaxyPosition.x * static_cast<float> (mapArea.getWidth()),
                                                   static_cast<float> (mapArea.getY()) + system.galaxyPosition.y * static_cast<float> (mapArea.getHeight()));
    juce::ColourGradient mapWash (juce::Colour::fromRGBA (26, 42, 96, 178),
                                  mapFloat.getX(), mapFloat.getY(),
                                  juce::Colour::fromRGBA (8, 16, 46, 34),
                                  mapFloat.getRight(), mapFloat.getBottom(),
                                  false);
    auto mapIdentity = identityColour.withMultipliedSaturation (1.1f).withAlpha (0.22f);
    mapWash.addColour (0.40, mapIdentity);
    g.setGradientFill (mapWash);
    g.fillRoundedRectangle (mapFloat.reduced (8.0f), 18.0f);

    juce::Path travelRings;
    for (int i = 0; i < 4; ++i)
    {
        const float w = mapFloat.getWidth() * (0.26f + 0.16f * static_cast<float> (i));
        const float h = mapFloat.getHeight() * (0.12f + 0.09f * static_cast<float> (i));
        travelRings.addEllipse (juce::Rectangle<float> (w, h).withCentre (selectedPoint));
    }
    g.setColour (identityColour.withAlpha (0.16f));
    g.strokePath (travelRings, juce::PathStrokeType (1.1f));

    fillGlow (g, juce::Rectangle<float> (mapArea.getX() + mapArea.getWidth() * 0.4f,
                                         mapArea.getY() + mapArea.getHeight() * 0.1f,
                                         mapArea.getWidth() * 0.42f,
                                         mapArea.getHeight() * 0.42f),
              juce::Colour (0xffffc16f), 0.20f);

    fillGlow (g, juce::Rectangle<float> (mapArea.getX() + mapArea.getWidth() * 0.10f,
                                         mapArea.getY() + mapArea.getHeight() * 0.44f,
                                         mapArea.getWidth() * 0.50f,
                                         mapArea.getHeight() * 0.34f),
              juce::Colour::fromRGB (92, 176, 255), 0.14f);
    fillGlow (g, juce::Rectangle<float> (mapArea.getX() + mapArea.getWidth() * 0.52f,
                                         mapArea.getY() + mapArea.getHeight() * 0.28f,
                                         mapArea.getWidth() * 0.26f,
                                         mapArea.getHeight() * 0.24f),
              juce::Colour::fromRGB (255, 112, 164), 0.10f);

    fillGlow (g, juce::Rectangle<float> (selectedPoint.x - mapArea.getWidth() * 0.20f,
                                         selectedPoint.y - mapArea.getHeight() * 0.18f,
                                         mapArea.getWidth() * 0.40f,
                                         mapArea.getHeight() * 0.36f),
              identityColour, 0.14f);

    juce::Random farDust (0x47614C41);
    for (int i = 0; i < 170; ++i)
    {
        const float x = mapFloat.getX() + farDust.nextFloat() * mapFloat.getWidth();
        const float y = mapFloat.getY() + farDust.nextFloat() * mapFloat.getHeight();
        const float size = 0.6f + farDust.nextFloat() * 1.4f;
        const float alpha = 0.05f + farDust.nextFloat() * 0.18f;
        g.setColour (juce::Colour::fromRGBA (188, 220, 255, static_cast<uint8_t> (alpha * 255.0f)));
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ x, y }));
    }

    juce::Path constellationLines;
    juce::Path routePath;
    for (int i = 0; i < galaxy.systems.size(); ++i)
    {
        const auto& system = *galaxy.systems.getUnchecked (i);
        const auto x = static_cast<float> (mapArea.getX()) + system.galaxyPosition.x * static_cast<float> (mapArea.getWidth());
        const auto y = static_cast<float> (mapArea.getY()) + system.galaxyPosition.y * static_cast<float> (mapArea.getHeight());

        float nearestDistance = std::numeric_limits<float>::max();
        juce::Point<float> nearestPoint;
        bool foundNeighbour = false;

        for (int j = 0; j < galaxy.systems.size(); ++j)
        {
            if (i == j)
                continue;

            const auto& other = *galaxy.systems.getUnchecked (j);
            const auto ox = static_cast<float> (mapArea.getX()) + other.galaxyPosition.x * static_cast<float> (mapArea.getWidth());
            const auto oy = static_cast<float> (mapArea.getY()) + other.galaxyPosition.y * static_cast<float> (mapArea.getHeight());
            const float dx = ox - x;
            const float dy = oy - y;
            const float distance = std::sqrt (dx * dx + dy * dy);
            if (distance < nearestDistance)
            {
                nearestDistance = distance;
                nearestPoint = { ox, oy };
                foundNeighbour = true;
            }
        }

        if (foundNeighbour && nearestDistance < mapArea.getWidth() * 0.26f)
        {
            constellationLines.startNewSubPath (x, y);
            constellationLines.lineTo (nearestPoint);
        }

        if (i == selectedSystemIndex)
            continue;

        const float dx = x - selectedPoint.x;
        const float dy = y - selectedPoint.y;
        const float distance = std::sqrt (dx * dx + dy * dy);
        if (distance < mapArea.getWidth() * 0.34f)
        {
            routePath.startNewSubPath (selectedPoint);
            routePath.lineTo (x, y);
        }
    }
    g.setColour (juce::Colour::fromRGBA (124, 196, 255, 30));
    g.strokePath (constellationLines, juce::PathStrokeType (1.0f));
    g.setColour (juce::Colour::fromRGBA (255, 224, 158, 58));
    g.strokePath (routePath, juce::PathStrokeType (1.4f));

    juce::Random starDust (0x4B4C414E);
    for (int i = 0; i < 90; ++i)
    {
        const float x = mapFloat.getX() + starDust.nextFloat() * mapFloat.getWidth();
        const float y = mapFloat.getY() + starDust.nextFloat() * mapFloat.getHeight();
        const float size = 1.0f + starDust.nextFloat() * 2.4f;
        const auto colour = juce::Colour::fromHSV (0.52f + starDust.nextFloat() * 0.18f,
                                                   0.25f + starDust.nextFloat() * 0.30f,
                                                   0.75f + starDust.nextFloat() * 0.25f,
                                                   0.20f + starDust.nextFloat() * 0.35f);
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (size, size).withCentre ({ x, y }));
    }

    for (int i = 0; i < galaxy.systems.size(); ++i)
    {
        const auto& system = *galaxy.systems.getUnchecked (i);
        const auto x = mapArea.getX() + static_cast<int> (system.galaxyPosition.x * static_cast<float> (mapArea.getWidth()));
        const auto y = mapArea.getY() + static_cast<int> (system.galaxyPosition.y * static_cast<float> (mapArea.getHeight()));
        const auto selected = i == selectedSystemIndex;
        const auto systemColour = system.planets[0]->accent
                                      .interpolatedWith (juce::Colour::fromRGB (120, 210, 255), 0.18f)
                                      .brighter (0.25f);

        fillGlow (g, juce::Rectangle<float> (static_cast<float> (x - 22), static_cast<float> (y - 22), 44.0f, 44.0f),
                  selected ? juce::Colour (0xffffe3ae) : systemColour, selected ? 0.48f : 0.24f);
        fillGlow (g, juce::Rectangle<float> (static_cast<float> (x - 10), static_cast<float> (y - 10), 20.0f, 20.0f),
                  selected ? juce::Colour (0xffffffff) : systemColour.brighter (0.4f), selected ? 0.22f : 0.10f);
        g.setColour (selected ? juce::Colour (0xfffff7e2) : systemColour.interpolatedWith (juce::Colour (0xfff8f0ff), 0.22f));
        g.fillEllipse (static_cast<float> (x - (selected ? 7 : 4)),
                       static_cast<float> (y - (selected ? 7 : 4)),
                       static_cast<float> (selected ? 14 : 8),
                       static_cast<float> (selected ? 14 : 8));
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, selected ? 224 : 132));
        g.fillEllipse (static_cast<float> (x - (selected ? 2.4f : 1.6f)),
                       static_cast<float> (y - (selected ? 2.4f : 1.6f)),
                       static_cast<float> (selected ? 4.8f : 3.2f),
                       static_cast<float> (selected ? 4.8f : 3.2f));

        if (selected)
        {
            g.setColour (juce::Colour (0xa0fff0d4));
            g.drawEllipse (static_cast<float> (x - 12), static_cast<float> (y - 12), 24.0f, 24.0f, 1.5f);
            g.setColour (juce::Colour::fromRGBA (255, 246, 214, 86));
            g.drawEllipse (static_cast<float> (x - 18), static_cast<float> (y - 18), 36.0f, 36.0f, 1.2f);
            g.setColour (juce::Colour::fromRGBA (255, 240, 208, 220));
            g.setFont (juce::FontOptions (12.0f * uiScale));
            g.drawFittedText (system.name.toUpperCase(),
                              juce::Rectangle<int> (x + 16, y - 12, 140, 24),
                              juce::Justification::centredLeft, 1);
        }
    }

    auto inner = sideArea.toFloat().reduced (16.0f * uiScale, 16.0f * uiScale);
    auto topRow = inner.removeFromTop (44.0f * uiScale);
    auto modePill = topRow.removeFromLeft (topRow.getWidth() * 0.36f);
    topRow.removeFromLeft (10.0f * uiScale);
    auto slabChip = topRow;

    g.setColour (juce::Colour::fromRGBA (34, 62, 96, 244));
    g.fillRoundedRectangle (modePill, 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 34));
    g.fillRoundedRectangle (modePill.reduced (3.0f, 3.0f).withHeight (modePill.getHeight() * 0.38f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (126, 240, 255, 200));
    g.drawRoundedRectangle (modePill, 8.0f, 1.6f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.5f * uiScale));
    g.drawFittedText ("BUILD MODE", modePill.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);

    g.setColour (juce::Colour::fromRGBA (15, 26, 62, 238));
    g.fillRoundedRectangle (slabChip, 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
    g.fillRoundedRectangle (slabChip.reduced (3.0f, 3.0f).withHeight (slabChip.getHeight() * 0.28f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (90, 176, 255, 112));
    g.drawRoundedRectangle (slabChip, 8.0f, 1.2f);
    g.setColour (juce::Colour::fromRGBA (198, 228, 255, 222));
    g.setFont (juce::FontOptions (12.5f * uiScale));
    g.drawFittedText ("GALAXY  " + galaxy.name.toUpperCase(),
                      slabChip.reduced (14.0f, 0.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    inner.removeFromTop (10.0f * uiScale);
    auto detailChip = inner.removeFromTop (56.0f * uiScale);
    g.setColour (juce::Colour::fromRGBA (12, 22, 52, 210));
    g.fillRoundedRectangle (detailChip, 8.0f);
    g.setColour (juce::Colour::fromRGBA (102, 182, 255, 46));
    g.drawRoundedRectangle (detailChip, 8.0f, 1.1f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f * uiScale));
    g.drawFittedText ("Chart routes through " + selectedPlanet.name + "'s sector   "
                      + getPlanetBuildModeName (selectedPlanet.assignedBuildMode) + " worlds shape differently   "
                      + getPlanetPerformanceModeName (selectedPlanet.assignedPerformanceMode) + " worlds sound differently",
                      detailChip.reduced (14.0f, 6.0f).toNearestInt(),
                      juce::Justification::centredLeft, 2);

    inner.removeFromTop (10.0f * uiScale);
    auto drawStat = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        g.setColour (juce::Colour::fromRGBA (12, 22, 52, 210));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour::fromRGBA (102, 182, 255, 46));
        g.drawRoundedRectangle (bounds, 7.0f, 1.1f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.24f), 3.0f);
        auto statInner = bounds.reduced (10.0f * uiScale, 5.0f * uiScale);
        g.setColour (juce::Colour::fromRGBA (152, 216, 255, 170));
        g.setFont (juce::FontOptions (10.0f * uiScale));
        g.drawFittedText (label, statInner.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f * uiScale));
        g.drawFittedText (value, statInner.toNearestInt(), juce::Justification::centredLeft, 1);
    };

    const float statGap = 10.0f * uiScale;
    auto statRowA = inner.removeFromTop (54.0f * uiScale);
    const float statWidthA = (statRowA.getWidth() - statGap * 2.0f) / 3.0f;
    drawStat (statRowA.removeFromLeft (statWidthA), "SYSTEM", juce::String (selectedSystemIndex + 1) + "/" + juce::String (galaxy.systems.size()));
    statRowA.removeFromLeft (statGap);
    drawStat (statRowA.removeFromLeft (statWidthA), "PLANET", juce::String (selectedPlanetIndex + 1) + "/" + juce::String (system.planets.size()));
    statRowA.removeFromLeft (statGap);
    drawStat (statRowA.removeFromLeft (statWidthA), "MODE", getPlanetBuildModeName (selectedPlanet.assignedBuildMode));

    inner.removeFromTop (8.0f * uiScale);
    auto statRowB = inner.removeFromTop (54.0f * uiScale);
    const float statWidthB = (statRowB.getWidth() - statGap) / 2.0f;
    drawStat (statRowB.removeFromLeft (statWidthB), "PERF", getPlanetPerformanceModeName (selectedPlanet.assignedPerformanceMode));
    statRowB.removeFromLeft (statGap);
    drawStat (statRowB.removeFromLeft (statWidthB), "DISCOVERY", juce::String (juce::roundToInt ((system.planets.size() / 7.0f) * 100.0f)) + "%");

    inner.removeFromTop (10.0f * uiScale);
    auto drawChip = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        g.setColour (juce::Colour::fromRGBA (11, 20, 46, 208));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour::fromRGBA (96, 184, 255, 46));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 16));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.20f), 3.0f);
        auto textArea = bounds.reduced (11.0f * uiScale, 0.0f);
        auto labelArea = textArea.removeFromLeft (juce::jmin (84.0f, bounds.getWidth() * 0.36f));
        g.setColour (juce::Colour::fromRGBA (132, 196, 255, 166));
        g.setFont (juce::FontOptions (11.0f * uiScale));
        g.drawFittedText (label, labelArea.toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f * uiScale));
        g.drawFittedText (value, textArea.toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (juce::Colour::fromRGBA (84, 226, 255, 24));
        g.fillRoundedRectangle (bounds.withTrimmedTop (bounds.getHeight() - 3.0f).reduced (1.0f, 0.0f), 6.0f);
    };

    const float gap = 10.0f * uiScale;
    auto infoRowA = inner.removeFromTop (30.0f * uiScale);
    const float chipW = (infoRowA.getWidth() - gap) / 2.0f;
    drawChip (infoRowA.removeFromLeft (chipW), "Systems", juce::String (galaxy.systems.size()));
    infoRowA.removeFromLeft (gap);
    drawChip (infoRowA.removeFromLeft (chipW), "Planets", juce::String (system.planets.size()));

    inner.removeFromTop (8.0f * uiScale);
    auto infoRowB = inner.removeFromTop (30.0f * uiScale);
    drawChip (infoRowB.removeFromLeft (chipW), "Orbit", juce::String (getSelectedPlanet().orbitIndex + 1));
    infoRowB.removeFromLeft (gap);
    drawChip (infoRowB.removeFromLeft (chipW), "Signal", juce::String (getSelectedPlanet().musicalRootHz, 1) + " Hz");

    inner.removeFromTop (10.0f * uiScale);
    auto listArea = inner.removeFromTop (juce::jmin (252.0f * uiScale, inner.getHeight() - 52.0f * uiScale));
    for (int i = 0; i < system.planets.size(); ++i)
    {
        const auto& planet = *system.planets.getUnchecked (i);
        auto row = listArea.removeFromTop (44.0f * uiScale);
        g.setColour (i == selectedPlanetIndex ? juce::Colour::fromRGBA (255, 168, 84, 72)
                                              : juce::Colour::fromRGBA (12, 22, 52, 182));
        g.fillRoundedRectangle (row, 8.0f);
        g.setColour (i == selectedPlanetIndex ? juce::Colour::fromRGBA (255, 212, 140, 188)
                                              : juce::Colour::fromRGBA (102, 182, 255, 38));
        g.drawRoundedRectangle (row, 8.0f, i == selectedPlanetIndex ? 1.7f : 1.1f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, i == selectedPlanetIndex ? 24 : 12));
        g.fillRoundedRectangle (row.reduced (3.0f, 3.0f).withHeight (row.getHeight() * 0.18f), 3.0f);
        g.setColour (planet.accent);
        g.fillEllipse (row.removeFromLeft (28.0f).reduced (4.0f));
        auto text = row.reduced (8.0f * uiScale, 5.0f * uiScale);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.5f * uiScale));
        g.drawFittedText (planet.name, text.removeFromTop (15.0f * uiScale).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (juce::Colour::fromRGBA (216, 232, 255, 226));
        g.setFont (juce::FontOptions (11.0f * uiScale));
        g.drawFittedText (getPlanetBuildModeName (planet.assignedBuildMode)
                          + " / " + getPlanetPerformanceModeName (planet.assignedPerformanceMode)
                          + "   water " + juce::String (planet.water, 2)
                          + "   energy " + juce::String (planet.energy, 2),
                          text.toNearestInt(), juce::Justification::centredLeft, 1);
        listArea.removeFromTop (6.0f * uiScale);
    }

    inner.removeFromTop (8.0f * uiScale);
    auto footerRow = inner.removeFromTop (42.0f * uiScale);
    g.setColour (juce::Colour::fromRGBA (12, 22, 52, 198));
    g.fillRoundedRectangle (footerRow, 8.0f);
    g.setColour (juce::Colour::fromRGBA (102, 182, 255, 42));
    g.drawRoundedRectangle (footerRow, 8.0f, 1.0f);
    g.setColour (juce::Colour::fromRGBA (216, 232, 255, 226));
    g.setFont (juce::FontOptions (11.5f * uiScale));
    g.drawFittedText ("Now charting  " + system.name + " / " + selectedPlanet.name + "   "
                      + getPlanetIdentitySummary (selectedPlanet),
                      footerRow.reduced (12.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 2);
}

void GameComponent::drawGalaxyLogbook (juce::Graphics& g, juce::Rectangle<int> area)
{
    logHeatmapHitTargets.clear();
    auto bounds = area.reduced (14);
    auto page = bounds.toFloat();

    g.setColour (juce::Colour::fromRGBA (24, 14, 10, 210));
    g.fillRoundedRectangle (page, 18.0f);

    auto paper = page.reduced (20.0f);
    juce::ColourGradient paperWash (juce::Colour::fromRGB (225, 211, 182), paper.getX(), paper.getY(),
                                    juce::Colour::fromRGB (198, 180, 148), paper.getRight(), paper.getBottom(), false);
    paperWash.addColour (0.48, juce::Colour::fromRGB (235, 224, 198));
    g.setGradientFill (paperWash);
    g.fillRoundedRectangle (paper, 12.0f);
    g.setColour (juce::Colour::fromRGBA (82, 46, 28, 180));
    g.drawRoundedRectangle (paper, 12.0f, 1.6f);

    g.setColour (juce::Colour::fromRGBA (120, 88, 62, 56));
    for (int i = 0; i < 22; ++i)
    {
        const float y = paper.getY() + 64.0f + i * 30.0f;
        if (y > paper.getBottom() - 24.0f)
            break;
        g.drawLine (paper.getX() + 34.0f, y, paper.getRight() - 28.0f, y, 0.8f);
    }

    auto header = paper.reduced (28.0f);
    auto titleArea = header.removeFromTop (54.0f);
    g.setColour (juce::Colour::fromRGB (73, 39, 26));
    g.setFont (juce::FontOptions (28.0f));
    g.drawText ("Exploration Logbook", titleArea.toNearestInt(), juce::Justification::centredLeft);
    g.setFont (juce::FontOptions (14.0f));
    g.setColour (juce::Colour::fromRGBA (86, 58, 42, 220));
    g.drawFittedText ("L closes the log   Esc returns to the map", header.removeFromTop (22.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    auto entries = persistence.getVisitLog();
    auto listArea = header.reduced (0.0f, 10.0f);
    galaxyLogScroll = juce::jlimit (0.0f, getGalaxyLogMaxScroll (area), galaxyLogScroll);

    if (entries.empty())
    {
        g.setColour (juce::Colour::fromRGBA (76, 54, 38, 220));
        g.setFont (juce::FontOptions (20.0f));
        g.drawFittedText ("No worlds entered yet. Land on a planet to add the first page.", listArea.toNearestInt(),
                          juce::Justification::centred, 2);
        return;
    }

    g.saveState();
    g.reduceClipRegion (listArea.toNearestInt());
    auto cursorArea = listArea.withY (listArea.getY() - galaxyLogScroll);
    const float rowHeight = 96.0f;
    const float rowGap = 8.0f;

    for (const auto& entry : entries)
    {
        auto row = cursorArea.removeFromTop (rowHeight);
        cursorArea.removeFromTop (rowGap);
        if (row.getBottom() < listArea.getY())
            continue;
        if (row.getY() > listArea.getBottom())
            break;

        g.setColour (juce::Colour::fromRGBA (104, 73, 54, 28));
        g.fillRoundedRectangle (row, 8.0f);
        g.setColour (juce::Colour::fromRGBA (92, 62, 44, 90));
        g.drawRoundedRectangle (row, 8.0f, 1.0f);

        auto ink = row.reduced (16.0f, 10.0f);
        auto preview = ink.removeFromRight (94.0f);
        preview.removeFromLeft (8.0f);
        auto dot = ink.removeFromLeft (18.0f).withSizeKeepingCentre (12.0f, 12.0f);
        g.setColour (entry.accent);
        g.fillEllipse (dot);
        ink.removeFromLeft (10.0f);

        auto top = ink.removeFromTop (22.0f);
        g.setColour (juce::Colour::fromRGB (70, 38, 26));
        g.setFont (juce::FontOptions (18.0f));
        g.drawFittedText (entry.planetName + "  |  " + entry.systemName, top.toNearestInt(), juce::Justification::centredLeft, 1);

        g.setFont (juce::FontOptions (13.0f));
        g.setColour (juce::Colour::fromRGBA (82, 58, 42, 230));
        auto mid = ink.removeFromTop (18.0f);
        g.drawFittedText ("orbit " + juce::String (entry.orbitIndex + 1)
                          + "   root " + juce::String (entry.musicalRootHz, 1) + " Hz"
                          + "   mode " + getPlanetBuildModeName (entry.assignedBuildMode)
                          + "   perf " + getPlanetPerformanceModeName (entry.assignedPerformanceMode)
                          + "   visits " + juce::String (entry.visitCount),
                          mid.toNearestInt(), juce::Justification::centredLeft, 1);

        auto bottom = ink.removeFromTop (18.0f);
        g.drawFittedText ("water " + juce::String (entry.water, 2)
                          + "   energy " + juce::String (entry.energy, 2)
                          + "   atmosphere " + juce::String (entry.atmosphere, 2),
                          bottom.toNearestInt(), juce::Justification::centredLeft, 1);

        auto stamp = ink.removeFromTop (16.0f);
        g.setColour (juce::Colour::fromRGBA (112, 40, 34, 210));
        g.drawFittedText ("last logged " + entry.lastVisitedUtc.replaceCharacter ('T', ' ')
                          + (entry.performanceSessions > 0
                                ? "   perf " + juce::String (entry.performanceSessions) + " runs / "
                                  + juce::String (entry.performanceSeconds, 1) + "s"
                                : juce::String()),
                          stamp.toNearestInt(), juce::Justification::centredLeft, 1);

        if (entry.performanceWidth > 0 && entry.performanceDepth > 0)
        {
            auto heatmap = preview.removeFromTop (72.0f);
            auto noteStrip = preview.removeFromBottom (10.0f);
            logHeatmapHitTargets.push_back ({ entry.planetId, heatmap.expanded (4.0f) });

            g.setColour (juce::Colour::fromRGBA (74, 52, 38, 48));
            g.fillRoundedRectangle (heatmap, 5.0f);
            g.setColour (juce::Colour::fromRGBA (92, 62, 44, 110));
            g.drawRoundedRectangle (heatmap, 5.0f, 1.0f);

            const int width = juce::jlimit (1, PlanetSurfaceState::maxWidth, entry.performanceWidth);
            const int depth = juce::jlimit (1, PlanetSurfaceState::maxDepth, entry.performanceDepth);
            const float cellW = heatmap.getWidth() / static_cast<float> (width);
            const float cellH = heatmap.getHeight() / static_cast<float> (depth);
            const int maxMovement = entry.performanceMovementHeat.empty() ? 0 : *std::max_element (entry.performanceMovementHeat.begin(), entry.performanceMovementHeat.end());
            const int maxTrigger = entry.performanceTriggerHeat.empty() ? 0 : *std::max_element (entry.performanceTriggerHeat.begin(), entry.performanceTriggerHeat.end());

            for (int y = 0; y < depth; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const size_t index = static_cast<size_t> (y * width + x);
                    const int movement = index < entry.performanceMovementHeat.size() ? entry.performanceMovementHeat[index] : 0;
                    const int trigger = index < entry.performanceTriggerHeat.size() ? entry.performanceTriggerHeat[index] : 0;
                    const float movementAlpha = maxMovement > 0 ? static_cast<float> (movement) / static_cast<float> (maxMovement) : 0.0f;
                    const float triggerAlpha = maxTrigger > 0 ? static_cast<float> (trigger) / static_cast<float> (maxTrigger) : 0.0f;

                    auto cell = juce::Rectangle<float> (heatmap.getX() + x * cellW,
                                                        heatmap.getY() + y * cellH,
                                                        cellW,
                                                        cellH).reduced (0.5f);
                    g.setColour (juce::Colour::fromFloatRGBA (0.20f, 0.42f, 0.86f, 0.10f + movementAlpha * 0.55f));
                    g.fillRect (cell);
                    if (triggerAlpha > 0.0f)
                    {
                        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.50f, 0.16f, 0.10f + triggerAlpha * 0.72f));
                        g.fillRect (cell.reduced (cellW * 0.12f, cellH * 0.12f));
                    }
                }
            }

            if (! entry.performanceNoteHeat.empty())
            {
                const int maxNote = *std::max_element (entry.performanceNoteHeat.begin(), entry.performanceNoteHeat.end());
                const float barW = noteStrip.getWidth() / static_cast<float> (entry.performanceNoteHeat.size());
                for (size_t i = 0; i < entry.performanceNoteHeat.size(); ++i)
                {
                    const float alpha = maxNote > 0 ? static_cast<float> (entry.performanceNoteHeat[i]) / static_cast<float> (maxNote) : 0.0f;
                    auto bar = juce::Rectangle<float> (noteStrip.getX() + static_cast<float> (i) * barW,
                                                       noteStrip.getY(),
                                                       juce::jmax (1.0f, barW - 0.5f),
                                                       noteStrip.getHeight());
                    const auto colour = getNoteColourForLayer (static_cast<int> (i));
                    g.setColour (colour.withAlpha (0.10f + alpha * 0.82f));
                    g.fillRect (bar);
                }
            }
        }
    }
    g.restoreState();

    if (! expandedLogHeatmapPlanetId.isEmpty())
    {
        const auto it = std::find_if (entries.begin(), entries.end(),
                                      [this] (const VisitLogEntry& entry) { return entry.planetId == expandedLogHeatmapPlanetId; });
        if (it != entries.end() && it->performanceWidth > 0 && it->performanceDepth > 0)
        {
            auto overlay = page.reduced (90.0f, 70.0f);
            g.setColour (juce::Colour::fromRGBA (16, 10, 8, 160));
            g.fillRoundedRectangle (page, 18.0f);
            g.setColour (juce::Colour::fromRGBA (240, 228, 206, 245));
            g.fillRoundedRectangle (overlay, 16.0f);
            g.setColour (juce::Colour::fromRGBA (82, 46, 28, 180));
            g.drawRoundedRectangle (overlay, 16.0f, 1.8f);

            auto content = overlay.reduced (22.0f);
            auto title = content.removeFromTop (28.0f);
            g.setColour (juce::Colour::fromRGB (70, 38, 26));
            g.setFont (juce::FontOptions (22.0f));
            g.drawText (it->planetName + " performance memory", title.toNearestInt(), juce::Justification::centredLeft);

            auto subtitle = content.removeFromTop (20.0f);
            g.setColour (juce::Colour::fromRGBA (90, 58, 42, 220));
            g.setFont (juce::FontOptions (13.0f));
            g.drawFittedText ("Click anywhere to close   "
                              + juce::String (it->performanceSessions) + " runs / "
                              + juce::String (it->performanceSeconds, 1) + "s logged",
                              subtitle.toNearestInt(), juce::Justification::centredLeft, 1);

            content.removeFromTop (10.0f);
            auto heatmap = content.removeFromTop (content.getHeight() * 0.78f);
            auto noteStrip = content.removeFromBottom (18.0f);

            g.setColour (juce::Colour::fromRGBA (74, 52, 38, 56));
            g.fillRoundedRectangle (heatmap, 8.0f);
            g.setColour (juce::Colour::fromRGBA (92, 62, 44, 110));
            g.drawRoundedRectangle (heatmap, 8.0f, 1.0f);

            const int width = juce::jlimit (1, PlanetSurfaceState::maxWidth, it->performanceWidth);
            const int depth = juce::jlimit (1, PlanetSurfaceState::maxDepth, it->performanceDepth);
            const float cellW = heatmap.getWidth() / static_cast<float> (width);
            const float cellH = heatmap.getHeight() / static_cast<float> (depth);
            const int maxMovement = it->performanceMovementHeat.empty() ? 0 : *std::max_element (it->performanceMovementHeat.begin(), it->performanceMovementHeat.end());
            const int maxTrigger = it->performanceTriggerHeat.empty() ? 0 : *std::max_element (it->performanceTriggerHeat.begin(), it->performanceTriggerHeat.end());

            for (int y = 0; y < depth; ++y)
            {
                for (int x = 0; x < width; ++x)
                {
                    const size_t index = static_cast<size_t> (y * width + x);
                    const int movement = index < it->performanceMovementHeat.size() ? it->performanceMovementHeat[index] : 0;
                    const int trigger = index < it->performanceTriggerHeat.size() ? it->performanceTriggerHeat[index] : 0;
                    const float movementAlpha = maxMovement > 0 ? static_cast<float> (movement) / static_cast<float> (maxMovement) : 0.0f;
                    const float triggerAlpha = maxTrigger > 0 ? static_cast<float> (trigger) / static_cast<float> (maxTrigger) : 0.0f;

                    auto cell = juce::Rectangle<float> (heatmap.getX() + x * cellW,
                                                        heatmap.getY() + y * cellH,
                                                        cellW,
                                                        cellH).reduced (0.8f);
                    g.setColour (juce::Colour::fromFloatRGBA (0.20f, 0.42f, 0.86f, 0.08f + movementAlpha * 0.58f));
                    g.fillRect (cell);
                    if (triggerAlpha > 0.0f)
                    {
                        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.50f, 0.16f, 0.10f + triggerAlpha * 0.76f));
                        g.fillRect (cell.reduced (cellW * 0.10f, cellH * 0.10f));
                    }
                }
            }

            if (! it->performanceNoteHeat.empty())
            {
                const int maxNote = *std::max_element (it->performanceNoteHeat.begin(), it->performanceNoteHeat.end());
                const float barW = noteStrip.getWidth() / static_cast<float> (it->performanceNoteHeat.size());
                for (size_t i = 0; i < it->performanceNoteHeat.size(); ++i)
                {
                    const float alpha = maxNote > 0 ? static_cast<float> (it->performanceNoteHeat[i]) / static_cast<float> (maxNote) : 0.0f;
                    auto bar = juce::Rectangle<float> (noteStrip.getX() + static_cast<float> (i) * barW,
                                                       noteStrip.getY(),
                                                       juce::jmax (1.0f, barW - 1.0f),
                                                       noteStrip.getHeight());
                    g.setColour (getNoteColourForLayer (static_cast<int> (i)).withAlpha (0.08f + alpha * 0.84f));
                    g.fillRect (bar);
                }
            }
        }
    }
}

float GameComponent::getGalaxyLogMaxScroll (juce::Rectangle<int> area)
{
    auto bounds = area.reduced (14);
    auto page = bounds.toFloat();
    auto paper = page.reduced (20.0f);
    auto header = paper.reduced (28.0f);
    header.removeFromTop (54.0f);
    header.removeFromTop (22.0f);
    const auto listArea = header.reduced (0.0f, 10.0f);
    const auto entryCount = static_cast<int> (persistence.getVisitLog().size());
    if (entryCount <= 0)
        return 0.0f;

    const float contentHeight = entryCount * 96.0f + juce::jmax (0, entryCount - 1) * 8.0f;
    return juce::jmax (0.0f, contentHeight - listArea.getHeight());
}

void GameComponent::drawLandingScene (juce::Graphics& g, juce::Rectangle<int> area)
{
    ensureActivePlanetLoaded();
    const auto& planet = getSelectedPlanet();
    const auto buildColour = getPlanetBuildModeColour (planet.assignedBuildMode);
    const auto performanceColour = getPlanetPerformanceModeColour (planet.assignedPerformanceMode);
    const auto identityColour = getPlanetIdentityColour (planet);

    auto left = area.removeFromLeft (area.proportionOfWidth (0.50f)).reduced (8);
    auto right = area.reduced (12);
    const float uiScale = juce::jlimit (0.82f, 1.0f,
                                        juce::jmin (right.getWidth() / 700.0f,
                                                    right.getHeight() / 860.0f));

    auto drawReferencePanel = [&] (juce::Rectangle<int> bounds)
    {
        auto panel = bounds.toFloat();
        juce::ColourGradient hudGradient (juce::Colour::fromRGBA (11, 18, 44, 238),
                                          panel.getX(), panel.getY(),
                                          juce::Colour::fromRGBA (16, 28, 68, 228),
                                          panel.getRight(), panel.getBottom(),
                                          false);
        hudGradient.addColour (0.38, juce::Colour::fromRGBA (34, 98, 198, 132));
        g.setGradientFill (hudGradient);
        g.fillRoundedRectangle (panel, 12.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 30));
        g.fillRoundedRectangle (panel.reduced (4.0f, 4.0f).withHeight (panel.getHeight() * 0.16f), 6.0f);
        g.setColour (juce::Colour::fromRGBA (118, 236, 255, 178));
        g.drawRoundedRectangle (panel, 12.0f, 1.8f);
        g.setColour (juce::Colour::fromRGBA (28, 54, 104, 255));
        g.drawRoundedRectangle (panel.reduced (4.0f), 8.0f, 1.0f);
    };

    drawReferencePanel (left);
    drawReferencePanel (right);

    juce::ColourGradient sky (identityColour.interpolatedWith (juce::Colour (0xffffc07b), 0.28f),
                              left.getX() + left.getWidth() * 0.62f,
                              static_cast<float> (left.getY()),
                              juce::Colour (0xff040303),
                              static_cast<float> (left.getX()),
                              static_cast<float> (left.getBottom()),
                              false);
    sky.addColour (0.34, buildColour.withAlpha (0.30f));
    sky.addColour (0.52, activePlanetState->skyColour.interpolatedWith (performanceColour, 0.22f).darker (0.45f));
    g.setGradientFill (sky);
    g.fillRoundedRectangle (left.toFloat(), 22.0f);

    fillGlow (g, juce::Rectangle<float> (left.getX() + left.getWidth() * 0.48f,
                                         left.getY() + 18.0f,
                                         left.getWidth() * 0.44f,
                                         left.getWidth() * 0.44f),
              performanceColour.interpolatedWith (juce::Colour (0xffffd1a0), 0.46f), 0.36f);
    g.setColour (identityColour.interpolatedWith (juce::Colour (0xffffd09a), 0.28f));
    g.fillEllipse (left.getCentreX() - 70.0f, left.getY() + 40.0f, 140.0f, 140.0f);

    juce::Path ridge;
    ridge.startNewSubPath (static_cast<float> (left.getX()), static_cast<float> (left.getBottom() - 110));
    ridge.lineTo (left.getX() + left.getWidth() * 0.18f, left.getBottom() - 150.0f);
    ridge.lineTo (left.getX() + left.getWidth() * 0.34f, left.getBottom() - 124.0f);
    ridge.lineTo (left.getX() + left.getWidth() * 0.52f, left.getBottom() - 176.0f);
    ridge.lineTo (left.getX() + left.getWidth() * 0.72f, left.getBottom() - 136.0f);
    ridge.lineTo (static_cast<float> (left.getRight()), static_cast<float> (left.getBottom() - 164));
    ridge.lineTo (static_cast<float> (left.getRight()), static_cast<float> (left.getBottom()));
    ridge.lineTo (static_cast<float> (left.getX()), static_cast<float> (left.getBottom()));
    ridge.closeSubPath();
    g.setColour (juce::Colour (0xee050403));
    g.fillPath (ridge);

    g.setColour (buildColour.withAlpha (0.18f));
    g.strokePath (ridge, juce::PathStrokeType (1.2f), juce::AffineTransform::translation (0.0f, -2.0f));

    g.setColour (juce::Colour (0x90000000));
    g.fillRect (left.removeFromBottom (110));

    g.setColour (warmInk());
    g.setFont (24.0f);
    g.drawText (planet.name + " approach", left.removeFromBottom (88), juce::Justification::centred);

    g.setFont (16.0f);
    g.drawText (getPlanetIdentitySummary (planet),
                left.removeFromBottom (40).reduced (18, 0), juce::Justification::centred, true);

    auto inner = right.toFloat().reduced (16.0f * uiScale, 16.0f * uiScale);
    auto topRow = inner.removeFromTop (44.0f * uiScale);
    auto modePill = topRow.removeFromLeft (topRow.getWidth() * 0.36f);
    topRow.removeFromLeft (10.0f * uiScale);
    auto slabChip = topRow;

    g.setColour (juce::Colour::fromRGBA (34, 62, 96, 244));
    g.fillRoundedRectangle (modePill, 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 34));
    g.fillRoundedRectangle (modePill.reduced (3.0f, 3.0f).withHeight (modePill.getHeight() * 0.38f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (126, 240, 255, 200));
    g.drawRoundedRectangle (modePill, 8.0f, 1.6f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.5f * uiScale));
    g.drawFittedText ("BUILD MODE", modePill.reduced (10.0f, 0.0f).toNearestInt(), juce::Justification::centred, 1);

    g.setColour (juce::Colour::fromRGBA (15, 26, 62, 238));
    g.fillRoundedRectangle (slabChip, 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
    g.fillRoundedRectangle (slabChip.reduced (3.0f, 3.0f).withHeight (slabChip.getHeight() * 0.28f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (90, 176, 255, 112));
    g.drawRoundedRectangle (slabChip, 8.0f, 1.2f);
    g.setColour (juce::Colour::fromRGBA (198, 228, 255, 222));
    g.setFont (juce::FontOptions (12.5f * uiScale));
    g.drawFittedText ("PLANET  " + planet.name.toUpperCase(),
                      slabChip.reduced (14.0f, 0.0f).toNearestInt(),
                      juce::Justification::centredLeft, 1);

    inner.removeFromTop (10.0f * uiScale);
    auto detailChip = inner.removeFromTop (56.0f * uiScale);
    g.setColour (juce::Colour::fromRGBA (12, 22, 52, 210));
    g.fillRoundedRectangle (detailChip, 8.0f);
    g.setColour (juce::Colour::fromRGBA (102, 182, 255, 46));
    g.drawRoundedRectangle (detailChip, 8.0f, 1.1f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.0f * uiScale));
    g.drawFittedText ("Enter descends into " + getPlanetBuildModeName (planet.assignedBuildMode).toLowerCase()
                      + " build   Esc returns to the galaxy   P later enters " + getPlanetPerformanceModeName (planet.assignedPerformanceMode).toLowerCase(),
                      detailChip.reduced (14.0f, 6.0f).toNearestInt(), juce::Justification::centredLeft, 2);

    inner.removeFromTop (10.0f * uiScale);
    auto drawStat = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        g.setColour (juce::Colour::fromRGBA (12, 22, 52, 210));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour::fromRGBA (102, 182, 255, 46));
        g.drawRoundedRectangle (bounds, 7.0f, 1.1f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.24f), 3.0f);
        auto statInner = bounds.reduced (10.0f * uiScale, 5.0f * uiScale);
        g.setColour (juce::Colour::fromRGBA (152, 216, 255, 170));
        g.setFont (juce::FontOptions (10.0f * uiScale));
        g.drawFittedText (label, statInner.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 1);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.0f * uiScale));
        g.drawFittedText (value, statInner.toNearestInt(), juce::Justification::centredLeft, 1);
    };
    const float statGap = 10.0f * uiScale;
    auto statRowA = inner.removeFromTop (54.0f * uiScale);
    const float statWidthA = (statRowA.getWidth() - statGap * 2.0f) / 3.0f;
    drawStat (statRowA.removeFromLeft (statWidthA), "BUILD", getPlanetBuildModeName (planet.assignedBuildMode));
    statRowA.removeFromLeft (statGap);
    drawStat (statRowA.removeFromLeft (statWidthA), "PERF", getPlanetPerformanceModeName (planet.assignedPerformanceMode));
    statRowA.removeFromLeft (statGap);
    drawStat (statRowA.removeFromLeft (statWidthA), "KEY", getNoteNameForLayer (1));

    inner.removeFromTop (8.0f * uiScale);
    auto statRowB = inner.removeFromTop (54.0f * uiScale);
    const float statWidthB = (statRowB.getWidth() - statGap) / 2.0f;
    drawStat (statRowB.removeFromLeft (statWidthB), "SCALE", getPerformanceScaleName());
    statRowB.removeFromLeft (statGap);
    drawStat (statRowB.removeFromLeft (statWidthB), "SYNTH", getPerformanceSynthName());

    inner.removeFromTop (10.0f * uiScale);
    auto drawInfoChip = [&] (juce::Rectangle<float> bounds, const juce::String& text)
    {
        g.setColour (juce::Colour::fromRGBA (12, 22, 52, 198));
        g.fillRoundedRectangle (bounds, 7.0f);
        g.setColour (juce::Colour::fromRGBA (102, 182, 255, 42));
        g.drawRoundedRectangle (bounds, 7.0f, 1.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 16));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.20f), 3.0f);
        g.setColour (juce::Colour::fromRGBA (216, 232, 255, 226));
        g.setFont (juce::FontOptions (11.5f * uiScale));
        g.drawFittedText (text, bounds.reduced (12.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft, 1);
    };
    const float infoGap = 8.0f * uiScale;
    auto infoRowA = inner.removeFromTop (30.0f * uiScale);
    const float infoWidth = (infoRowA.getWidth() - infoGap) / 2.0f;
    drawInfoChip (infoRowA.removeFromLeft (infoWidth), "Orbit  " + juce::String (planet.orbitIndex + 1));
    infoRowA.removeFromLeft (infoGap);
    drawInfoChip (infoRowA.removeFromLeft (infoWidth), "Root  " + juce::String (planet.musicalRootHz, 1) + " Hz");

    inner.removeFromTop (8.0f * uiScale);
    auto infoRowB = inner.removeFromTop (30.0f * uiScale);
    drawInfoChip (infoRowB.removeFromLeft (infoWidth), "Water  " + juce::String (planet.water, 2));
    infoRowB.removeFromLeft (infoGap);
    drawInfoChip (infoRowB.removeFromLeft (infoWidth), "Energy  " + juce::String (planet.energy, 2));

    inner.removeFromTop (8.0f * uiScale);
    auto infoRowC = inner.removeFromTop (30.0f * uiScale);
    drawInfoChip (infoRowC, "Build  " + getPlanetBuildModeName (planet.assignedBuildMode)
                            + "   |   Performance  " + getPlanetPerformanceModeName (planet.assignedPerformanceMode));

    inner.removeFromTop (10.0f * uiScale);
    auto bodyChip = inner.removeFromTop (68.0f * uiScale);
    drawInfoChip (bodyChip, getPlanetBuildModeFlavour (planet.assignedBuildMode)
                            + ". " + getPlanetPerformanceModeFlavour (planet.assignedPerformanceMode)
                            + ". If you built here before, the resident voxel stack has already been restored.");

    inner.removeFromTop (10.0f * uiScale);
    auto preview = inner.removeFromTop (juce::jmin (220.0f * uiScale, inner.getHeight() - 52.0f * uiScale)).toNearestInt().reduced (6);
    drawReferencePanel (preview);
    const int tileSize = juce::jmax (1, juce::jmin (preview.getWidth() / getSurfaceWidth(),
                                                    preview.getHeight() / getSurfaceDepth()));
    const int gridWidth = tileSize * getSurfaceWidth();
    const int gridHeight = tileSize * getSurfaceDepth();
    const int gridX = preview.getX() + (preview.getWidth() - gridWidth) / 2;
    const int gridY = preview.getY() + (preview.getHeight() - gridHeight) / 2;

    for (int y = 0; y < getSurfaceDepth(); ++y)
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            int topZ = 0;
            for (int z = getSurfaceHeight() - 1; z >= 0; --z)
            {
                if (activePlanetState->getBlock (x, y, z) != 0)
                {
                    topZ = z;
                    break;
                }
            }

            g.setColour (getNoteColourForLayer (topZ));
            g.fillRect (gridX + x * tileSize, gridY + y * tileSize, juce::jmax (1, tileSize - 1), juce::jmax (1, tileSize - 1));
        }

    inner.removeFromTop (8.0f * uiScale);
    auto footerRow = inner.removeFromTop (42.0f * uiScale);
    drawInfoChip (footerRow, "Enter descends   Esc returns   surface data is resident and persistent on disk");
}

void GameComponent::drawBuilderScene (juce::Graphics& g, juce::Rectangle<int> area)
{
    ensureActivePlanetLoaded();

    if (! performanceMode
        && builderViewMode == BuilderViewMode::firstPerson
        && topDownBuildMode == TopDownBuildMode::none)
    {
        drawFirstPersonBuilder (g, area.reduced (8));
        return;
    }

    area = area.reduced (14);
    auto gridArea = area.removeFromLeft (area.proportionOfWidth (0.64f));
    area.removeFromLeft (18);
    auto infoArea = area.reduced (10);
    gridArea = gridArea.reduced (10).withTrimmedLeft (10).withTrimmedBottom (10);
    infoArea = infoArea.withY (gridArea.getY()).withHeight (gridArea.getHeight());
    const float uiScale = juce::jlimit (0.80f, 1.0f,
                                        juce::jmin (infoArea.getWidth() / 700.0f,
                                                    infoArea.getHeight() / 900.0f));
    auto drawReferencePanel = [&] (juce::Rectangle<int> bounds)
    {
        auto panel = bounds.toFloat();
        juce::ColourGradient hudGradient (juce::Colour::fromRGBA (11, 18, 44, 238),
                                          panel.getX(), panel.getY(),
                                          juce::Colour::fromRGBA (16, 28, 68, 228),
                                          panel.getRight(), panel.getBottom(),
                                          false);
        hudGradient.addColour (0.38, juce::Colour::fromRGBA (34, 98, 198, 132));
        g.setGradientFill (hudGradient);
        g.fillRoundedRectangle (panel, 12.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 30));
        g.fillRoundedRectangle (panel.reduced (4.0f, 4.0f).withHeight (panel.getHeight() * 0.16f), 6.0f);
        g.setColour (juce::Colour::fromRGBA (118, 236, 255, 178));
        g.drawRoundedRectangle (panel, 12.0f, 1.8f);
        g.setColour (juce::Colour::fromRGBA (28, 54, 104, 255));
        g.drawRoundedRectangle (panel.reduced (4.0f), 8.0f, 1.0f);
    };

    drawReferencePanel (gridArea);
    drawReferencePanel (infoArea);

    if (performanceMode)
        drawPerformanceView (g, gridArea.toFloat());
    else if (topDownBuildMode == TopDownBuildMode::tetris)
        drawTetrisBuildView (g, gridArea.toFloat());
    else if (topDownBuildMode == TopDownBuildMode::cellularAutomata)
        drawAutomataBuildView (g, gridArea.toFloat());
    else if (builderViewMode == BuilderViewMode::firstPerson)
        drawFirstPersonBuilder (g, gridArea);
    else
        drawIsometricBuilder (g, gridArea);

    auto inner = infoArea.toFloat().reduced (18.0f * uiScale, 16.0f * uiScale);
    auto drawCard = [&] (juce::Rectangle<float> bounds, float radius = 8.0f)
    {
        g.setColour (juce::Colour::fromRGBA (13, 23, 54, 224));
        g.fillRoundedRectangle (bounds, radius);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 18));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.16f), 3.0f);
        g.setColour (juce::Colour::fromRGBA (102, 182, 255, 52));
        g.drawRoundedRectangle (bounds, radius, 1.1f);
    };

    auto drawLabel = [&] (juce::Rectangle<float> bounds, const juce::String& label)
    {
        g.setColour (juce::Colour::fromRGBA (152, 216, 255, 170));
        g.setFont (juce::FontOptions (10.5f * uiScale));
        g.drawText (label, bounds.toNearestInt(), juce::Justification::centredLeft);
    };

    auto drawValueCard = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value)
    {
        drawCard (bounds, 8.0f);
        auto text = bounds.reduced (14.0f * uiScale, 10.0f * uiScale);
        drawLabel (text.removeFromTop (16.0f * uiScale), label);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (14.5f * uiScale));
        g.drawFittedText (value, text.toNearestInt(), juce::Justification::centredLeft, 1);
    };

    if (performanceMode)
    {
        drawPerformanceSidebar (g, infoArea.toFloat());
        return;
    }

    auto headerCard = inner.removeFromTop (60.0f * uiScale);
    drawCard (headerCard, 9.0f);
    auto headerInner = headerCard.reduced (14.0f * uiScale, 10.0f * uiScale);
    auto titleRow = headerInner.removeFromTop (22.0f * uiScale);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (16.0f * uiScale));
    g.drawText ((topDownBuildMode == TopDownBuildMode::tetris ? "TETRIS BUILD"
                  : topDownBuildMode == TopDownBuildMode::cellularAutomata ? "CELLULAR AUTOMATA"
                  : "ISOMETRIC EDIT"),
                titleRow.toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colour::fromRGBA (186, 224, 255, 210));
    g.setFont (juce::FontOptions (11.5f * uiScale));
    g.drawFittedText (getSelectedPlanet().name.toUpperCase() + "   |   "
                      + (topDownBuildMode == TopDownBuildMode::none ? getIsometricChordName().toUpperCase()
                                                                    : getTopDownBuildModeName().toUpperCase()),
                      headerInner.toNearestInt(), juce::Justification::centredLeft, 1);

    inner.removeFromTop (14.0f * uiScale);
    auto controlsCard = inner.removeFromTop (84.0f * uiScale);
    drawCard (controlsCard, 9.0f);
    auto controlsInner = controlsCard.reduced (14.0f * uiScale, 10.0f * uiScale);
    drawLabel (controlsInner.removeFromTop (16.0f * uiScale), "CONTROLS");
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (11.6f * uiScale));
    const juce::String controlsText = topDownBuildMode == TopDownBuildMode::tetris
                                        ? "Move arrows or mouse   Layer [ ]   Notes 1-4   Chord V\nRotate R or left click   New N   Soft drop S   Hard drop Space/right click   Place O   Delete X"
                                        : topDownBuildMode == TopDownBuildMode::cellularAutomata
                                            ? "Move arrows   Layer [ ]   Notes 1-4\nSeed click/O   Delete X   Randomise N   E evolve next layer"
                                            : "Pan WASD   Rotate Q/E   Height [ ]   Notes 1-4\nCycle chord V   Place click/O   Delete X   Exit Esc";
    g.drawFittedText (controlsText,
                      controlsInner.toNearestInt(),
                      juce::Justification::topLeft,
                      3);

    inner.removeFromTop (14.0f * uiScale);
    int filledCount = 0;
    for (int z = 0; z < getSurfaceHeight(); ++z)
        for (int y = 0; y < getSurfaceDepth(); ++y)
            for (int x = 0; x < getSurfaceWidth(); ++x)
                filledCount += activePlanetState->getBlock (x, y, z) != 0 ? 1 : 0;

    const float gap = 10.0f * uiScale;
    auto statsTop = inner.removeFromTop (60.0f * uiScale);
    const float twoCol = (statsTop.getWidth() - gap) / 2.0f;
    drawValueCard (statsTop.removeFromLeft (twoCol), topDownBuildMode == TopDownBuildMode::tetris ? "PIECE"
                                                  : topDownBuildMode == TopDownBuildMode::cellularAutomata ? "RULE"
                                                  : "ROOT",
                   topDownBuildMode == TopDownBuildMode::tetris ? getTetrominoTypeName (tetrisPiece.active ? tetrisPiece.type : nextTetrisType)
                   : topDownBuildMode == TopDownBuildMode::cellularAutomata ? juce::String (((automataBuildLayer % 4) == 1) ? "Coral"
                                                                                             : ((automataBuildLayer % 4) == 2) ? "Fredkin"
                                                                                             : ((automataBuildLayer % 4) == 3) ? "DayNight"
                                                                                                                           : "Life")
                   : getNoteNameForLayer (builderLayer));
    statsTop.removeFromLeft (gap);
    drawValueCard (statsTop.removeFromLeft (twoCol), "NOTES", juce::String (isometricPlacementHeight));

    inner.removeFromTop (12.0f * uiScale);
    auto statsBottom = inner.removeFromTop (60.0f * uiScale);
    drawValueCard (statsBottom.removeFromLeft (twoCol), "CURSOR",
                   topDownBuildMode == TopDownBuildMode::cellularAutomata && automataHoverCell.has_value()
                     ? "x" + juce::String (automataHoverCell->x) + "  y" + juce::String (automataHoverCell->y) + "  z" + juce::String (automataBuildLayer)
                     : topDownBuildMode == TopDownBuildMode::tetris
                         ? "x" + juce::String (tetrisPiece.anchor.x) + "  y" + juce::String (tetrisPiece.anchor.y) + "  z" + juce::String (tetrisBuildLayer)
                         : "x" + juce::String (builderCursorX) + "  y" + juce::String (builderCursorY) + "  z" + juce::String (builderLayer));
    statsBottom.removeFromLeft (gap);
    drawValueCard (statsBottom.removeFromLeft (twoCol), "ZOOM", juce::String (isometricCamera.zoom, 2));

    inner.removeFromTop (16.0f * uiScale);
    auto chordCard = inner.removeFromTop (136.0f * uiScale);
    drawCard (chordCard, 9.0f);
    auto chordInner = chordCard.reduced (12.0f * uiScale, 10.0f * uiScale);
    drawLabel (chordInner.removeFromTop (16.0f * uiScale), topDownBuildMode == TopDownBuildMode::cellularAutomata ? "AUTOMATA COLOURS" : "CHORD TYPE");
    chordInner.removeFromTop (10.0f * uiScale);

    auto drawChordChip = [&] (juce::Rectangle<float> bounds, const juce::String& text, bool active)
    {
        g.setColour (active ? juce::Colour::fromRGBA (255, 168, 84, 104)
                            : juce::Colour::fromRGBA (20, 32, 68, 228));
        g.fillRoundedRectangle (bounds, 8.0f);
        g.setColour (active ? juce::Colour::fromRGBA (255, 224, 168, 226)
                            : juce::Colour::fromRGBA (102, 182, 255, 48));
        g.drawRoundedRectangle (bounds, 8.0f, active ? 1.7f : 1.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, active ? 28 : 12));
        g.fillRoundedRectangle (bounds.reduced (3.0f, 3.0f).withHeight (bounds.getHeight() * 0.18f), 3.0f);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (11.2f * uiScale));
        g.drawFittedText (text, bounds.toNearestInt(), juce::Justification::centred, 1);
    };

    const std::array<IsometricChordType, 8> chordTypes {
        IsometricChordType::single, IsometricChordType::power, IsometricChordType::majorTriad, IsometricChordType::minorTriad,
        IsometricChordType::sus2, IsometricChordType::sus4, IsometricChordType::majorSeventh, IsometricChordType::minorSeventh
    };
    const std::array<juce::String, 8> chordNames { "Single", "Power", "Major", "Minor", "Sus2", "Sus4", "Maj7", "Min7" };
    auto chordTop = chordInner.removeFromTop (46.0f * uiScale);
    chordInner.removeFromTop (10.0f * uiScale);
    auto chordBottom = chordInner.removeFromTop (46.0f * uiScale);
    const float chipGap = 8.0f * uiScale;
    const float chipWidth = (chordTop.getWidth() - chipGap * 3.0f) / 4.0f;
    for (size_t i = 0; i < chordTypes.size(); ++i)
    {
        auto& row = i < 4 ? chordTop : chordBottom;
        auto chip = row.removeFromLeft (chipWidth);
        if ((i % 4) != 3)
            row.removeFromLeft (chipGap);
        drawChordChip (chip, chordNames[i], chordTypes[i] == isometricChordType);
    }

    inner.removeFromTop (16.0f * uiScale);
    auto footerCard = inner.removeFromTop (66.0f * uiScale);
    drawCard (footerCard, 9.0f);
    auto footerInner = footerCard.reduced (14.0f * uiScale, 10.0f * uiScale);
    drawLabel (footerInner.removeFromTop (16.0f * uiScale), "STATUS");
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (11.4f * uiScale));
    g.drawFittedText ((topDownBuildMode == TopDownBuildMode::tetris
                         ? "Filled " + juce::String (filledCount) + "   |   Next " + getTetrominoTypeName (nextTetrisType)
                           + "   |   E climbs to next layer"
                         : topDownBuildMode == TopDownBuildMode::cellularAutomata
                             ? "Filled " + juce::String (filledCount) + "   |   E evolves into layer "
                               + juce::String (juce::jmin (getSurfaceHeight() - 1, automataBuildLayer + 1))
                             : "Filled " + juce::String (filledCount) + "   |   Rotation " + juce::String (isometricCamera.rotation)
                               + "   |   Mouse place/remove active"),
                      footerInner.toNearestInt(),
                      juce::Justification::topLeft,
                      2);
}

void GameComponent::drawPerformanceView (juce::Graphics& g, juce::Rectangle<float> area)
{
    const auto board = getPerformanceBoardBounds (area);
    const float tileSize = board.getWidth() / static_cast<float> (getSurfaceWidth());
    const float pulse = 0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0034));
    const float beatPulse = 0.5f + 0.5f * std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0032);
    const float barPulse = 0.5f + 0.5f * std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0011);

    juce::ColourGradient boardGlow (juce::Colour::fromRGBA (42, 102, 212, 120),
                                    board.getCentreX(), board.getCentreY() - board.getHeight() * 0.15f,
                                    juce::Colour::fromRGBA (8, 13, 36, 0),
                                    board.getCentreX(), board.getBottom() + 70.0f,
                                    true);
    g.setGradientFill (boardGlow);
    g.fillEllipse (board.expanded (86.0f, 74.0f));
    g.setColour (juce::Colour::fromRGBA (120, 220, 255, static_cast<uint8_t> (18 + 20 * pulse)));
    g.drawEllipse (board.expanded (66.0f, 56.0f), 2.0f + pulse * 1.2f);
    g.setColour (juce::Colour::fromRGBA (120, 220, 255, static_cast<uint8_t> (10 + 60 * barPulse)));
    g.drawEllipse (board.expanded (82.0f + 20.0f * barPulse, 68.0f + 18.0f * barPulse), 1.1f + 1.2f * barPulse);

    g.setColour (juce::Colour::fromRGBA (6, 11, 30, 236));
    g.fillRoundedRectangle (board.expanded (26.0f), 28.0f);
    g.setColour (juce::Colour::fromRGBA (74, 144, 255, 72));
    g.drawRoundedRectangle (board.expanded (26.0f), 28.0f, 1.8f);

    const auto activeRegion = getPerformanceRegionBounds();

    for (int y = 0; y < getSurfaceDepth(); ++y)
    {
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            auto cell = juce::Rectangle<float> (board.getX() + x * tileSize,
                                                board.getY() + y * tileSize,
                                                tileSize,
                                                tileSize);
            const bool inRegion = activeRegion.contains (x, y);
            const auto colour = inRegion ? juce::Colour::fromRGBA (8, 14, 34, 214)
                                         : juce::Colour::fromRGBA (8, 14, 34, 130);
            g.setColour (colour);
            g.fillRect (cell);

            std::vector<int> notes;
            notes.reserve (static_cast<size_t> (getSurfaceHeight()));
            for (int z = 1; z < getSurfaceHeight(); ++z)
                if (activePlanetState->getBlock (x, y, z) != 0)
                    notes.push_back (z);

            if (! notes.empty())
            {
                auto innerCell = cell.reduced (2.0f);
                const float sliceHeight = innerCell.getHeight() / static_cast<float> (notes.size());
                for (size_t i = 0; i < notes.size(); ++i)
                {
                    auto sliceBounds = juce::Rectangle<float> (innerCell.getX(),
                                                               innerCell.getBottom() - sliceHeight * static_cast<float> (i + 1) + 1.0f,
                                                               innerCell.getWidth(),
                                                               juce::jmax (1.0f, sliceHeight - 2.0f));
                    auto sliceColour = getNoteColourForLayer (notes[i]).interpolatedWith (juce::Colours::white, 0.08f);
                    if (! inRegion)
                    {
                        sliceColour = sliceColour.interpolatedWith (juce::Colour::greyLevel (sliceColour.getPerceivedBrightness()), 0.78f)
                                                 .withMultipliedAlpha (0.4f);
                    }

                    g.setColour (sliceColour);
                    g.fillRoundedRectangle (sliceBounds, 2.4f);
                }
            }

            g.setColour (juce::Colour::fromRGBA (88, 122, 214, inRegion ? 36 : 18));
            g.drawRect (cell, 1.0f);
        }
    }

    const auto regionLocal = juce::Rectangle<float> (board.getX() + static_cast<float> (activeRegion.getX()) * tileSize,
                                                     board.getY() + static_cast<float> (activeRegion.getY()) * tileSize,
                                                     static_cast<float> (activeRegion.getWidth()) * tileSize,
                                                     static_cast<float> (activeRegion.getHeight()) * tileSize);
    g.setColour (juce::Colour::fromRGBA (85, 210, 255, 34));
    g.fillRoundedRectangle (regionLocal.expanded (3.0f), 12.0f);
    g.setColour (juce::Colour::fromRGBA (144, 240, 255, 182));
    g.drawRoundedRectangle (regionLocal.expanded (2.5f), 12.0f, 2.2f);

    if (performanceAgentMode == PerformanceAgentMode::tenori)
    {
        const int column = juce::jlimit (activeRegion.getX(), activeRegion.getRight() - 1, performanceTenoriColumn);
        auto playhead = juce::Rectangle<float> (board.getX() + static_cast<float> (column) * tileSize,
                                                board.getY() + static_cast<float> (activeRegion.getY()) * tileSize,
                                                tileSize,
                                                static_cast<float> (activeRegion.getHeight()) * tileSize);
        g.setColour (juce::Colour::fromRGBA (108, 232, 255, 42));
        g.fillRoundedRectangle (playhead.expanded (tileSize * 0.12f, tileSize * 0.08f), 8.0f);
        g.setColour (juce::Colour::fromRGBA (148, 244, 255, 218));
        g.fillRoundedRectangle (playhead.reduced (tileSize * 0.18f, 0.0f), 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.34f));
        g.drawRoundedRectangle (playhead.reduced (tileSize * 0.18f, 0.0f), 6.0f, 1.4f);
    }

    for (const auto& track : performanceTracks)
    {
        if (! activeRegion.contains (track.cell))
            continue;

        auto cell = juce::Rectangle<float> (board.getX() + track.cell.x * tileSize,
                                            board.getY() + track.cell.y * tileSize,
                                            tileSize,
                                            tileSize).reduced (tileSize * 0.16f);
        g.setColour (juce::Colour::fromRGBA (80, 230, 255, 110));
        if (track.horizontal)
            g.drawLine (cell.getX(), cell.getCentreY(), cell.getRight(), cell.getCentreY(), 3.0f);
        else
            g.drawLine (cell.getCentreX(), cell.getY(), cell.getCentreX(), cell.getBottom(), 3.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 56));
        g.drawRoundedRectangle (cell, 4.0f, 1.0f);

        if (performanceSelection.kind == PerformanceSelection::Kind::track && performanceSelection.cell == track.cell)
        {
            g.setColour (juce::Colour::fromRGBA (255, 255, 255, 180));
            g.drawRoundedRectangle (cell.expanded (tileSize * 0.12f), 6.0f, 2.2f);
        }
    }

    for (const auto& orbitCenter : performanceOrbitCenters)
    {
        if (! activeRegion.contains (orbitCenter))
            continue;

        auto cell = juce::Rectangle<float> (board.getX() + orbitCenter.x * tileSize,
                                            board.getY() + orbitCenter.y * tileSize,
                                            tileSize,
                                            tileSize).reduced (tileSize * 0.10f);
        g.setColour (juce::Colour::fromRGBA (255, 170, 120, 42));
        g.fillEllipse (cell.expanded (tileSize * 0.18f));
        g.setColour (juce::Colour::fromRGBA (255, 200, 140, 210));
        g.drawEllipse (cell, 2.2f);
        g.drawEllipse (cell.reduced (tileSize * 0.12f), 1.2f);
        g.drawLine (cell.getCentreX(), cell.getY(), cell.getCentreX(), cell.getBottom(), 1.3f);
        g.drawLine (cell.getX(), cell.getCentreY(), cell.getRight(), cell.getCentreY(), 1.3f);
    }

    for (const auto& ripple : performanceRipples)
    {
        auto centreCell = juce::Rectangle<float> (board.getX() + ripple.centre.x * tileSize,
                                                  board.getY() + ripple.centre.y * tileSize,
                                                  tileSize,
                                                  tileSize);
        const auto centre = centreCell.getCentre();
        const float ringRadius = tileSize * (0.4f + static_cast<float> (ripple.radius) * 0.95f);
        const auto ringBounds = juce::Rectangle<float> (ringRadius * 2.0f, ringRadius * 2.0f).withCentre (centre);

        g.setColour (ripple.colour.withAlpha (0.10f));
        g.fillEllipse (ringBounds.expanded (tileSize * 0.16f));
        g.setColour (ripple.colour.withAlpha (0.74f));
        g.drawEllipse (ringBounds, 2.4f);
        g.setColour (juce::Colours::white.withAlpha (0.28f));
        g.drawEllipse (ringBounds.reduced (tileSize * 0.10f), 1.0f);

        auto core = centreCell.reduced (tileSize * 0.34f);
        g.setColour (ripple.colour.withAlpha (0.78f));
        g.fillEllipse (core);
        g.setColour (juce::Colours::white.withAlpha (0.64f));
        g.fillEllipse (core.reduced (tileSize * 0.18f));
    }

    for (const auto& sequencer : performanceSequencers)
    {
        if (! activeRegion.contains (sequencer.cell))
            continue;

        const auto cell = juce::Rectangle<float> (board.getX() + sequencer.cell.x * tileSize,
                                                  board.getY() + sequencer.cell.y * tileSize,
                                                  tileSize,
                                                  tileSize);
        const auto centre = cell.getCentre();

        if (sequencer.hasPreviousCell && activeRegion.contains (sequencer.previousCell))
        {
            const auto previousCell = juce::Rectangle<float> (board.getX() + sequencer.previousCell.x * tileSize,
                                                              board.getY() + sequencer.previousCell.y * tileSize,
                                                              tileSize,
                                                              tileSize);
            juce::Path trail;
            trail.startNewSubPath (previousCell.getCentre());
            trail.lineTo (centre);

            g.setColour (sequencer.colour.withAlpha (0.12f + 0.08f * pulse));
            g.strokePath (trail, juce::PathStrokeType (tileSize * 0.42f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
            g.setColour (sequencer.colour.withAlpha (0.88f));
            g.strokePath (trail, juce::PathStrokeType (tileSize * 0.12f,
                                                       juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }

        auto glow = cell.reduced (tileSize * 0.04f);
        auto head = cell.reduced (tileSize * 0.22f);
        g.setColour (sequencer.colour.withAlpha (0.14f + 0.10f * beatPulse));
        g.fillEllipse (glow.expanded (tileSize * 0.18f));
        g.setColour (sequencer.colour.withAlpha (0.26f + 0.16f * pulse));
        g.fillRoundedRectangle (head.expanded (tileSize * 0.16f), 8.0f);
        g.setColour (sequencer.colour.withAlpha (0.96f));
        g.fillRoundedRectangle (head, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.80f));
        g.drawRoundedRectangle (head, 6.0f, 1.8f);

        const juce::Point<float> dir (static_cast<float> (sequencer.direction.x),
                                      static_cast<float> (sequencer.direction.y));
        if (sequencer.direction.x != 0 || sequencer.direction.y != 0)
        {
            const juce::Point<float> perp (-dir.y, dir.x);
            const float arrowRadius = head.getWidth() * 0.18f;
            const auto arrowTip = centre + dir * (head.getWidth() * 0.30f);
            const auto arrowBase = centre - dir * (head.getWidth() * 0.02f);
            juce::Path arrow;
            arrow.startNewSubPath (arrowBase + perp * arrowRadius);
            arrow.lineTo (arrowTip);
            arrow.lineTo (arrowBase - perp * arrowRadius);
            arrow.closeSubPath();
            g.setColour (juce::Colour::fromRGBA (8, 16, 34, 190));
            g.fillPath (arrow);
        }
    }

    for (const auto& cellPoint : performanceAutomataCells)
    {
        if (! activeRegion.contains (cellPoint))
            continue;

        auto cell = juce::Rectangle<float> (board.getX() + cellPoint.x * tileSize,
                                            board.getY() + cellPoint.y * tileSize,
                                            tileSize,
                                            tileSize).reduced (tileSize * 0.18f);
        g.setColour (juce::Colour::fromRGBA (255, 180, 120, 44));
        g.fillRoundedRectangle (cell.expanded (tileSize * 0.16f), 4.0f);
        g.setColour (juce::Colour::fromRGBA (255, 214, 180, 230));
        g.fillRoundedRectangle (cell, 4.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 160));
        g.drawRoundedRectangle (cell, 4.0f, 1.2f);
    }

    for (const auto& snake : performanceSnakes)
    {
        if (snake.body.empty())
            continue;

        juce::Path spine;
        std::vector<juce::Point<float>> centres;
        centres.reserve (snake.body.size());

        for (const auto& segment : snake.body)
        {
            auto cell = juce::Rectangle<float> (board.getX() + segment.x * tileSize,
                                                board.getY() + segment.y * tileSize,
                                                tileSize,
                                                tileSize);
            centres.push_back (cell.getCentre());
        }

        if (! centres.empty())
        {
            spine.startNewSubPath (centres.front());
            for (size_t i = 1; i < centres.size(); ++i)
                spine.lineTo (centres[i]);

            g.setColour (snake.colour.withAlpha (0.18f));
            g.strokePath (spine, juce::PathStrokeType (tileSize * 0.34f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (snake.colour.withAlpha (0.10f + 0.08f * pulse));
            g.strokePath (spine, juce::PathStrokeType (tileSize * 0.52f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (snake.colour.withAlpha (0.78f));
            g.strokePath (spine, juce::PathStrokeType (tileSize * 0.16f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        for (size_t i = 0; i < snake.body.size(); ++i)
        {
            const auto cellPoint = snake.body[i];
            if (! activeRegion.contains (cellPoint))
                continue;

            auto cell = juce::Rectangle<float> (board.getX() + cellPoint.x * tileSize,
                                                board.getY() + cellPoint.y * tileSize,
                                                tileSize,
                                                tileSize);
            auto orb = cell.reduced (i == 0 ? tileSize * 0.18f : tileSize * 0.24f);
            auto segmentColour = snake.colour;
            if (i > 0)
                segmentColour = segmentColour.darker (static_cast<float> (i) * 0.05f);

            g.setColour (segmentColour.withAlpha (i == 0 ? 0.22f : 0.12f));
            g.fillEllipse (orb.expanded (tileSize * 0.12f));
            g.setColour (segmentColour.withAlpha (i == 0 ? 0.98f : 0.84f));
            g.fillEllipse (orb);
            g.setColour (juce::Colours::white.withAlpha (i == 0 ? 0.78f : 0.26f));
            g.drawEllipse (orb, i == 0 ? 1.8f : 1.0f);

            if (i == 0)
            {
                const auto centre = orb.getCentre();
                const juce::Point<float> dir (static_cast<float> (snake.direction.x), static_cast<float> (snake.direction.y));
                const juce::Point<float> perp (-dir.y, dir.x);
                const auto eyeOffset = perp * (orb.getWidth() * 0.14f);
                const auto eyeForward = dir * (orb.getWidth() * 0.12f);
                const float eyeSize = juce::jmax (2.0f, orb.getWidth() * 0.10f);

                g.setColour (juce::Colour::fromRGBA (6, 10, 24, 220));
                g.fillEllipse (juce::Rectangle<float> (eyeSize, eyeSize).withCentre (centre + eyeForward + eyeOffset));
                g.fillEllipse (juce::Rectangle<float> (eyeSize, eyeSize).withCentre (centre + eyeForward - eyeOffset));
            }
        }
    }

    for (const auto& disc : performanceDiscs)
    {
        auto cell = juce::Rectangle<float> (board.getX() + disc.cell.x * tileSize,
                                            board.getY() + disc.cell.y * tileSize,
                                            tileSize,
                                            tileSize).reduced (tileSize * 0.10f);
        const auto centre = cell.getCentre();
        const float radius = cell.getWidth() * 0.38f;
        const bool hovered = performanceHoverCell.has_value() && *performanceHoverCell == disc.cell;
        const auto glowColour = hovered ? juce::Colour::fromRGBA (255, 250, 180, 170)
                                        : juce::Colour::fromRGBA (90, 240, 255, 150);
        const auto ringColour = hovered ? juce::Colour::fromRGBA (255, 248, 210, 250)
                                        : juce::Colour::fromRGBA (170, 244, 255, 245);
        const auto coreColour = hovered ? juce::Colour::fromRGBA (34, 44, 86, 250)
                                        : juce::Colour::fromRGBA (10, 22, 58, 245);

        g.setColour (glowColour.withAlpha (hovered ? 0.30f : 0.22f));
        g.fillEllipse (cell.expanded (tileSize * 0.20f));
        g.setColour (glowColour.withAlpha (0.12f + 0.10f * pulse));
        g.fillEllipse (cell.expanded (tileSize * 0.34f));

        g.setColour (coreColour);
        g.fillEllipse (cell);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 64));
        g.fillEllipse (cell.reduced (tileSize * 0.18f).withTrimmedBottom (tileSize * 0.22f));

        g.setColour (ringColour);
        g.drawEllipse (cell, hovered ? 3.2f : 2.4f);
        g.drawEllipse (cell.reduced (tileSize * 0.10f), hovered ? 1.8f : 1.3f);

        const juce::Point<float> dir (static_cast<float> (disc.direction.x), static_cast<float> (disc.direction.y));
        const juce::Point<float> perp (-dir.y, dir.x);
        const auto tip = centre + dir * (radius * 1.12f);
        const auto base = centre - dir * (radius * 0.14f);
        const auto tailStart = centre - dir * (radius * 0.58f);
        const auto tailEnd = centre + dir * (radius * 0.40f);

        juce::Path pointerShadow;
        pointerShadow.startNewSubPath (base + perp * (radius * 0.34f));
        pointerShadow.lineTo (tip + dir * (radius * 0.10f));
        pointerShadow.lineTo (base - perp * (radius * 0.34f));
        pointerShadow.closeSubPath();
        g.setColour (glowColour.withAlpha (hovered ? 0.28f : 0.20f));
        g.fillPath (pointerShadow);

        g.setColour (ringColour.withAlpha (0.70f));
        g.drawLine ({ tailStart, tailEnd }, hovered ? 4.2f : 3.4f);
        g.setColour (juce::Colours::white.withAlpha (0.80f));
        g.drawLine ({ centre - dir * (radius * 0.44f), centre + dir * (radius * 0.26f) }, hovered ? 1.8f : 1.4f);

        juce::Path arrow;
        arrow.startNewSubPath (base + perp * (radius * 0.28f));
        arrow.lineTo (tip);
        arrow.lineTo (base - perp * (radius * 0.28f));
        arrow.closeSubPath();
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 238));
        g.fillPath (arrow);
        g.setColour (juce::Colour::fromRGBA (8, 16, 34, 190));
        g.strokePath (arrow, juce::PathStrokeType (1.2f));

        auto nose = juce::Rectangle<float> (radius * 0.34f, radius * 0.34f)
                        .withCentre (centre + dir * (radius * 0.62f));
        g.setColour (hovered ? juce::Colour::fromRGBA (255, 248, 210, 245)
                             : juce::Colour::fromRGBA (120, 240, 255, 235));
        g.fillEllipse (nose);

        juce::Path crosshair;
        crosshair.startNewSubPath (centre.x - radius * 0.34f, centre.y);
        crosshair.lineTo (centre.x + radius * 0.34f, centre.y);
        crosshair.startNewSubPath (centre.x, centre.y - radius * 0.34f);
        crosshair.lineTo (centre.x, centre.y + radius * 0.34f);
        g.setColour (ringColour.withAlpha (0.55f));
        g.strokePath (crosshair, juce::PathStrokeType (1.1f));

        if (performanceSelection.kind == PerformanceSelection::Kind::disc && performanceSelection.cell == disc.cell)
        {
            g.setColour (juce::Colours::white.withAlpha (0.86f));
            g.drawEllipse (cell.expanded (tileSize * 0.16f), 2.4f);
        }
    }

    for (auto flash : performanceFlashes)
    {
        auto cell = juce::Rectangle<float> (board.getX() + flash.cell.x * tileSize,
                                            board.getY() + flash.cell.y * tileSize,
                                            tileSize,
                                            tileSize).reduced (tileSize * 0.10f);
        g.setColour (flash.colour.withAlpha (flash.life * (flash.pulse ? 0.46f : 0.28f)));
        g.fillRoundedRectangle (cell.expanded (8.0f * flash.life), 8.0f);
    }

    if (performanceHoverCell.has_value())
    {
        auto hover = juce::Rectangle<float> (board.getX() + performanceHoverCell->x * tileSize,
                                             board.getY() + performanceHoverCell->y * tileSize,
                                             tileSize,
                                             tileSize).reduced (tileSize * 0.08f);
        g.setColour (juce::Colour::fromRGBA (110, 240, 255, 88));
        g.fillRoundedRectangle (hover, 6.0f);
        g.setColour (juce::Colour::fromRGBA (160, 248, 255, 220));
        g.drawRoundedRectangle (hover, 6.0f, 2.0f);

        auto previewCell = juce::Rectangle<float> (board.getX() + performanceHoverCell->x * tileSize,
                                                   board.getY() + performanceHoverCell->y * tileSize,
                                                   tileSize,
                                                   tileSize).reduced (tileSize * 0.14f);
        if (performancePlacementMode == PerformancePlacementMode::placeDisc)
        {
            const auto centre = previewCell.getCentre();
            const float radius = previewCell.getWidth() * 0.34f;
            const juce::Point<float> dir (static_cast<float> (performanceSelectedDirection.x),
                                          static_cast<float> (performanceSelectedDirection.y));
            const juce::Point<float> perp (-dir.y, dir.x);
            juce::Path arrow;
            const auto tip = centre + dir * (radius * 1.12f);
            const auto base = centre - dir * (radius * 0.14f);
            arrow.startNewSubPath (base + perp * (radius * 0.28f));
            arrow.lineTo (tip);
            arrow.lineTo (base - perp * (radius * 0.28f));
            arrow.closeSubPath();
            g.setColour (juce::Colour::fromRGBA (255, 255, 255, 42));
            g.fillEllipse (previewCell.expanded (tileSize * 0.24f));
            g.setColour (juce::Colour::fromRGBA (255, 236, 200, 210));
            g.fillPath (arrow);
            g.setColour (juce::Colour::fromRGBA (255, 255, 255, 180));
            g.drawEllipse (previewCell, 1.8f);
        }
        else if (performancePlacementMode == PerformancePlacementMode::placeTrack)
        {
            g.setColour (juce::Colour::fromRGBA (255, 255, 255, 42));
            g.fillRoundedRectangle (previewCell.expanded (tileSize * 0.16f), 5.0f);
            g.setColour (juce::Colour::fromRGBA (220, 246, 255, 220));
            if (performanceTrackHorizontal)
                g.drawLine (previewCell.getX(), previewCell.getCentreY(), previewCell.getRight(), previewCell.getCentreY(), 3.0f);
            else
                g.drawLine (previewCell.getCentreX(), previewCell.getY(), previewCell.getCentreX(), previewCell.getBottom(), 3.0f);
        }
    }

    g.setColour (juce::Colour::fromRGBA (126, 224, 255, 190));
    g.drawRoundedRectangle (board.expanded (5.0f), 12.0f, 2.2f);
    g.setColour (juce::Colour::fromRGBA (190, 244, 255, static_cast<uint8_t> (18 + 74 * beatPulse)));
    g.drawRoundedRectangle (board.expanded (9.0f + 6.0f * beatPulse), 16.0f, 1.2f + 1.6f * beatPulse);
}

void GameComponent::drawPerformanceSidebar (juce::Graphics& g, juce::Rectangle<float> area)
{
    auto panel = area.reduced (10.0f);
    juce::ColourGradient sidebarGradient (juce::Colour::fromRGBA (8, 14, 34, 238),
                                         panel.getX(), panel.getY(),
                                         juce::Colour::fromRGBA (16, 28, 66, 224),
                                         panel.getRight(), panel.getBottom(),
                                         false);
    g.setGradientFill (sidebarGradient);
    g.fillRoundedRectangle (panel, 24.0f);
    g.setColour (juce::Colour::fromRGBA (124, 220, 255, 92));
    g.drawRoundedRectangle (panel, 24.0f, 1.4f);

    auto inner = panel.reduced (18.0f, 18.0f);
    auto badge = inner.removeFromTop (34.0f);
    g.setColour (juce::Colour::fromRGBA (92, 236, 255, 26));
    g.fillRoundedRectangle (badge, 16.0f);
    g.setColour (juce::Colour::fromRGBA (124, 236, 255, 150));
    g.drawRoundedRectangle (badge, 16.0f, 1.2f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (15.0f));
    g.drawText ("PERFORMANCE MODE", badge.toNearestInt(), juce::Justification::centred);

    inner.removeFromTop (14.0f);
    auto drawCard = [&] (juce::Rectangle<float> bounds, const juce::String& label, const juce::String& value, const juce::String& sub)
    {
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 15));
        g.fillRoundedRectangle (bounds, 18.0f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, 24));
        g.drawRoundedRectangle (bounds, 18.0f, 1.0f);
        auto content = bounds.reduced (14.0f, 12.0f);
        g.setColour (juce::Colour::fromRGBA (150, 216, 255, 168));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText (label, content.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (20.0f));
        g.drawText (value, content.removeFromTop (24.0f).toNearestInt(), juce::Justification::centredLeft);
        g.setColour (juce::Colour::fromRGBA (218, 232, 255, 170));
        g.setFont (juce::FontOptions (12.5f));
        g.drawFittedText (sub, content.toNearestInt(), juce::Justification::centredLeft, 2);
    };

    auto card = inner.removeFromTop (82.0f);
    const auto regionName = performanceRegionMode == 0 ? "Centre 50%"
                          : performanceRegionMode == 1 ? "Centre 75%"
                          : "Full";
    drawCard (card, "REGION", regionName, "Z cycle arena");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "TEMPO", juce::String (performanceBpm, 1) + " BPM", ", / . slower faster");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    const int activeAgentVisualCount = performanceAgentMode == PerformanceAgentMode::automata
                                         ? static_cast<int> (performanceAutomataCells.size())
                                         : performanceAgentMode == PerformanceAgentMode::ripple
                                             ? static_cast<int> (performanceRipples.size())
                                             : performanceAgentMode == PerformanceAgentMode::sequencer
                                                 ? static_cast<int> (performanceSequencers.size())
                                             : performanceAgentMode == PerformanceAgentMode::tenori
                                                 ? 1
                                             : performanceAgentCount;
    drawCard (card, "AGENTS", getPerformanceAgentModeName(), juce::String (activeAgentVisualCount) + " active | 0-8 count");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "TOOLS", juce::String (static_cast<int> (performanceDiscs.size())) + " discs",
              juce::String (static_cast<int> (performanceTracks.size())) + " tracks | "
                + juce::String (static_cast<int> (performanceOrbitCenters.size())) + " centres");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "TOOL", getPerformancePlacementModeName(),
              performanceSelection.isValid() ? "Selected cell " + juce::String (performanceSelection.cell.x) + "," + juce::String (performanceSelection.cell.y)
                                             : "No selection");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "SOUND", getPerformanceSynthName(),
              getPerformanceDrumName() + " drums");
    inner.removeFromTop (16.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "NOTE TRIG", getSnakeTriggerModeName(),
              "H toggle trigger mode");
    inner.removeFromTop (10.0f);

    card = inner.removeFromTop (82.0f);
    drawCard (card, "SCALE", getPerformanceScaleName(),
              "Key " + getPerformanceKeyName());
    inner.removeFromTop (16.0f);

    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 12));
    g.fillRoundedRectangle (inner, 18.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 24));
    g.drawRoundedRectangle (inner, 18.0f, 1.0f);
    auto infoBlock = inner.reduced (14.0f, 12.0f);
    g.setColour (juce::Colour::fromRGBA (150, 216, 255, 168));
    g.setFont (juce::FontOptions (11.0f));
    g.drawText ("CONTROLS", infoBlock.removeFromTop (12.0f).toNearestInt(), juce::Justification::centredLeft);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f));
    const juce::String help =
        "P return to build\n"
        "Click place/select\n"
        "Backspace delete selected\n"
        "Arrow keys disc direction\n"
        "Y disc tool / rotate dir\n"
        "T track tool / rotate track\n"
        "U select tool\n"
        "I toggle orbit centre\n"
        "N cycle agent mode\n"
              "M synth  B beat on/off\n"
        "K key  L scale\n"
        "H head/whole body\n"
        ", / . tempo\n"
        "Z cycle region\n"
        "0-8 agent count\n"
        "Esc back";
    g.drawFittedText (help, infoBlock.toNearestInt(), juce::Justification::topLeft, 8);
}

void GameComponent::drawIsometricBuilder (juce::Graphics& g, juce::Rectangle<int> area)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const auto boardArea = area.toFloat().reduced (8.0f);
    const float pulsePhase = static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.006));
    const float beatPulse = 0.5f + 0.5f * pulsePhase;
    const float juicePulse = 0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.008));

    juce::ColourGradient boardGlow (juce::Colour::fromRGBA (42, 102, 212, static_cast<uint8_t> (90 + 30 * beatPulse)),
                                    boardArea.getCentreX(), boardArea.getCentreY() - boardArea.getHeight() * 0.15f,
                                    juce::Colour::fromRGBA (8, 13, 36, 0),
                                    boardArea.getCentreX(), boardArea.getBottom() + 70.0f, true);
    g.setGradientFill (boardGlow);
    g.fillEllipse (boardArea.expanded (86.0f, 74.0f));
    g.setColour (juce::Colour::fromRGBA (120, 220, 255, static_cast<uint8_t> (18 + 20 * juicePulse)));
    g.drawEllipse (boardArea.expanded (66.0f, 56.0f), 2.0f + juicePulse * 1.2f);
    g.setColour (juce::Colour::fromRGBA (120, 220, 255, static_cast<uint8_t> (10 + 60 * beatPulse)));
    g.drawEllipse (boardArea.expanded (82.0f + 20.0f * beatPulse, 68.0f + 18.0f * beatPulse), 1.1f + 1.2f * beatPulse);
    g.setColour (juce::Colour::fromRGBA (6, 11, 30, 236));
    g.fillRoundedRectangle (boardArea.expanded (26.0f), 28.0f);
    g.setColour (juce::Colour::fromRGBA (74, 144, 255, 72));
    g.drawRoundedRectangle (boardArea.expanded (26.0f), 28.0f, 1.8f);

    juce::Path floorPlane;
    const auto floorA = projectIsometricPoint (0, 0, 0, boardArea);
    const auto floorB = projectIsometricPoint (getSurfaceWidth(), 0, 0, boardArea);
    const auto floorC = projectIsometricPoint (getSurfaceWidth(), getSurfaceDepth(), 0, boardArea);
    const auto floorD = projectIsometricPoint (0, getSurfaceDepth(), 0, boardArea);
    floorPlane.startNewSubPath (floorA);
    floorPlane.lineTo (floorB);
    floorPlane.lineTo (floorC);
    floorPlane.lineTo (floorD);
    floorPlane.closeSubPath();
    g.setColour (juce::Colour::fromRGBA (10, 18, 42, 255));
    g.fillPath (floorPlane);

    juce::Path footprintTopOutline;
    const auto topOutlineA = projectIsometricPoint (0, 0, 1, boardArea);
    const auto topOutlineB = projectIsometricPoint (getSurfaceWidth(), 0, 1, boardArea);
    const auto topOutlineC = projectIsometricPoint (getSurfaceWidth(), getSurfaceDepth(), 1, boardArea);
    const auto topOutlineD = projectIsometricPoint (0, getSurfaceDepth(), 1, boardArea);
    footprintTopOutline.startNewSubPath (topOutlineA);
    footprintTopOutline.lineTo (topOutlineB);
    footprintTopOutline.lineTo (topOutlineC);
    footprintTopOutline.lineTo (topOutlineD);
    footprintTopOutline.closeSubPath();

    juce::Path gridPath;
    const auto gridStep = getIsometricGridLineStep();
    for (int x = 0; x <= getSurfaceWidth(); x += gridStep)
    {
        gridPath.startNewSubPath (projectIsometricPoint (x, 0, 0, boardArea));
        gridPath.lineTo (projectIsometricPoint (x, getSurfaceDepth(), 0, boardArea));
    }

    for (int y = 0; y <= getSurfaceDepth(); y += gridStep)
    {
        gridPath.startNewSubPath (projectIsometricPoint (0, y, 0, boardArea));
        gridPath.lineTo (projectIsometricPoint (getSurfaceWidth(), y, 0, boardArea));
    }

    g.setColour (juce::Colour::fromRGBA (95, 122, 214, 34));
    g.strokePath (gridPath, juce::PathStrokeType (1.1f));

    struct Direction
    {
        int dx = 0;
        int dy = 0;
    };

    struct FilledVoxel
    {
        int x = 0;
        int y = 0;
        int z = 0;
        int block = 0;
    };

    auto isEmpty = [&] (int x, int y, int z)
    {
        return ! juce::isPositiveAndBelow (x, getSurfaceWidth())
            || ! juce::isPositiveAndBelow (y, getSurfaceDepth())
            || ! juce::isPositiveAndBelow (z, getSurfaceHeight())
            || activePlanetState->getBlock (x, y, z) == 0;
    };

    Direction leftFaceDirection;
    Direction rightFaceDirection;
    bool hasLeftFaceDirection = false;
    bool hasRightFaceDirection = false;
    const std::array<Direction, 4> cardinalDirections { Direction { 1, 0 }, Direction { -1, 0 }, Direction { 0, 1 }, Direction { 0, -1 } };
    const auto rotatedOrigin = rotateIsometricXY (0, 0);

    for (const auto& direction : cardinalDirections)
    {
        const auto rotatedNeighbour = rotateIsometricXY (direction.dx, direction.dy);
        const int rx = rotatedNeighbour.x - rotatedOrigin.x;
        const int ry = rotatedNeighbour.y - rotatedOrigin.y;
        const float screenDx = (static_cast<float> (rx - ry) * refIsoTileWidth * isometricCamera.zoom) * 0.5f;
        const float screenDy = (static_cast<float> (rx + ry) * refIsoTileHeight * isometricCamera.zoom) * 0.5f;

        if (screenDy <= 0.0f)
            continue;

        if (screenDx < 0.0f)
        {
            leftFaceDirection = direction;
            hasLeftFaceDirection = true;
        }
        else if (screenDx > 0.0f)
        {
            rightFaceDirection = direction;
            hasRightFaceDirection = true;
        }
    }

    std::vector<FilledVoxel> renderVoxels;
    renderVoxels.reserve (static_cast<size_t> (getSurfaceWidth() * getSurfaceDepth() * getSurfaceHeight()));
    for (int z = 0; z < getSurfaceHeight(); ++z)
        for (int y = 0; y < getSurfaceDepth(); ++y)
            for (int x = 0; x < getSurfaceWidth(); ++x)
                if (const auto block = activePlanetState->getBlock (x, y, z); block != 0)
                    renderVoxels.push_back ({ x, y, z, block });

    std::sort (renderVoxels.begin(), renderVoxels.end(),
               [this] (const FilledVoxel& a, const FilledVoxel& b)
               {
                   const auto ra = rotateIsometricXY (a.x, a.y);
                   const auto rb = rotateIsometricXY (b.x, b.y);
                   const int depthA = ra.x + ra.y + a.z * 2;
                   const int depthB = rb.x + rb.y + b.z * 2;
                   if (depthA != depthB)
                       return depthA < depthB;
                   if (a.z != b.z)
                       return a.z < b.z;
                   if (a.y != b.y)
                       return a.y < b.y;
                   return a.x < b.x;
               });

    for (const auto& voxel : renderVoxels)
    {
                const int x = voxel.x;
                const int y = voxel.y;
                const int z = voxel.z;
                const auto aBottom = projectIsometricPoint (x,     y,     z, boardArea);
                const auto bBottom = projectIsometricPoint (x + 1, y,     z, boardArea);
                const auto cBottom = projectIsometricPoint (x + 1, y + 1, z, boardArea);
                const auto dBottom = projectIsometricPoint (x,     y + 1, z, boardArea);
                const auto aTop = projectIsometricPoint (x,     y,     z + 1, boardArea);
                const auto bTop = projectIsometricPoint (x + 1, y,     z + 1, boardArea);
                const auto cTop = projectIsometricPoint (x + 1, y + 1, z + 1, boardArea);
                const auto dTop = projectIsometricPoint (x,     y + 1, z + 1, boardArea);

                const bool showTop = (z == getSurfaceHeight() - 1) || isEmpty (x, y, z + 1);
                const bool showLeft = hasLeftFaceDirection
                                      ? isEmpty (x + leftFaceDirection.dx, y + leftFaceDirection.dy, z)
                                      : true;
                const bool showRight = hasRightFaceDirection
                                       ? isEmpty (x + rightFaceDirection.dx, y + rightFaceDirection.dy, z)
                                       : true;

                const auto colour = getNoteColourForLayer (z);
                bool selected = false;
                if (x == builderCursorX && y == builderCursorY)
                {
                    const auto intervals = getActiveChordStackIntervals();
                    for (const int interval : intervals)
                        if (z == builderLayer + interval)
                            {
                                selected = true;
                                break;
                            }
                }

                juce::Path topFace;
                if (showTop)
                {
                    topFace.startNewSubPath (aTop);
                    topFace.lineTo (bTop);
                    topFace.lineTo (cTop);
                    topFace.lineTo (dTop);
                    topFace.closeSubPath();
                }

                auto buildSideFace = [] (const Direction& direction,
                                         const juce::Point<float>& aTopPoint,
                                         const juce::Point<float>& bTopPoint,
                                         const juce::Point<float>& cTopPoint,
                                         const juce::Point<float>& dTopPoint,
                                         const juce::Point<float>& aBottomPoint,
                                         const juce::Point<float>& bBottomPoint,
                                         const juce::Point<float>& cBottomPoint,
                                         const juce::Point<float>& dBottomPoint)
                {
                    juce::Path path;
                    if (direction.dx == -1 && direction.dy == 0)
                    {
                        path.startNewSubPath (aTopPoint);
                        path.lineTo (dTopPoint);
                        path.lineTo (dBottomPoint);
                        path.lineTo (aBottomPoint);
                    }
                    else if (direction.dx == 1 && direction.dy == 0)
                    {
                        path.startNewSubPath (bTopPoint);
                        path.lineTo (cTopPoint);
                        path.lineTo (cBottomPoint);
                        path.lineTo (bBottomPoint);
                    }
                    else if (direction.dx == 0 && direction.dy == -1)
                    {
                        path.startNewSubPath (aTopPoint);
                        path.lineTo (bTopPoint);
                        path.lineTo (bBottomPoint);
                        path.lineTo (aBottomPoint);
                    }
                    else
                    {
                        path.startNewSubPath (dTopPoint);
                        path.lineTo (cTopPoint);
                        path.lineTo (cBottomPoint);
                        path.lineTo (dBottomPoint);
                    }

                    path.closeSubPath();
                    return path;
                };

                juce::Path leftFace;
                if (showLeft)
                    leftFace = buildSideFace (leftFaceDirection, aTop, bTop, cTop, dTop, aBottom, bBottom, cBottom, dBottom);

                juce::Path rightFace;
                if (showRight)
                    rightFace = buildSideFace (rightFaceDirection, aTop, bTop, cTop, dTop, aBottom, bBottom, cBottom, dBottom);

                juce::Path edgePath;
                if (showTop)
                {
                    edgePath.startNewSubPath (aTop);
                    edgePath.lineTo (bTop);
                    edgePath.lineTo (cTop);
                    edgePath.lineTo (dTop);
                    edgePath.closeSubPath();
                }
                if (showLeft)
                {
                    if (leftFaceDirection.dx == -1 && leftFaceDirection.dy == 0)
                    {
                        edgePath.startNewSubPath (aTop);
                        edgePath.lineTo (aBottom);
                        edgePath.lineTo (dBottom);
                        edgePath.lineTo (dTop);
                    }
                    else if (leftFaceDirection.dx == 1 && leftFaceDirection.dy == 0)
                    {
                        edgePath.startNewSubPath (bTop);
                        edgePath.lineTo (bBottom);
                        edgePath.lineTo (cBottom);
                        edgePath.lineTo (cTop);
                    }
                    else if (leftFaceDirection.dx == 0 && leftFaceDirection.dy == -1)
                    {
                        edgePath.startNewSubPath (aTop);
                        edgePath.lineTo (aBottom);
                        edgePath.lineTo (bBottom);
                        edgePath.lineTo (bTop);
                    }
                    else
                    {
                        edgePath.startNewSubPath (dTop);
                        edgePath.lineTo (dBottom);
                        edgePath.lineTo (cBottom);
                        edgePath.lineTo (cTop);
                    }
                }
                if (showRight)
                {
                    if (rightFaceDirection.dx == -1 && rightFaceDirection.dy == 0)
                    {
                        edgePath.startNewSubPath (aTop);
                        edgePath.lineTo (aBottom);
                        edgePath.lineTo (dBottom);
                        edgePath.lineTo (dTop);
                    }
                    else if (rightFaceDirection.dx == 1 && rightFaceDirection.dy == 0)
                    {
                        edgePath.startNewSubPath (bTop);
                        edgePath.lineTo (bBottom);
                        edgePath.lineTo (cBottom);
                        edgePath.lineTo (cTop);
                    }
                    else if (rightFaceDirection.dx == 0 && rightFaceDirection.dy == -1)
                    {
                        edgePath.startNewSubPath (aTop);
                        edgePath.lineTo (aBottom);
                        edgePath.lineTo (bBottom);
                        edgePath.lineTo (bTop);
                    }
                    else
                    {
                        edgePath.startNewSubPath (dTop);
                        edgePath.lineTo (dBottom);
                        edgePath.lineTo (cBottom);
                        edgePath.lineTo (cTop);
                    }
                }

                if (showTop)
                {
                    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.52f).withAlpha (0.10f));
                    g.fillPath (topFace);
                    g.setColour (colour.interpolatedWith (juce::Colours::white, 0.15f));
                    g.fillPath (topFace);
                    if (((x + y + z) % 9) == 0)
                    {
                        const auto centre = topFace.getBounds().getCentre();
                        g.setColour (juce::Colour::fromRGBA (255, 255, 255, static_cast<uint8_t> (18 + 20 * juicePulse)));
                        g.fillEllipse (juce::Rectangle<float> (3.0f + 2.0f * juicePulse, 3.0f + 2.0f * juicePulse).withCentre (centre));
                    }
                }
                if (showLeft)
                {
                    g.setColour (colour.darker (0.12f));
                    g.fillPath (leftFace);
                }
                if (showRight)
                {
                    g.setColour (colour.darker (0.28f));
                    g.fillPath (rightFace);
                }
                g.setColour (juce::Colour::fromRGBA (8, 10, 20, static_cast<uint8_t> (138 - 36 * beatPulse)));
                g.strokePath (edgePath, juce::PathStrokeType (1.0f));

                if (selected)
                {
                    g.setColour (juce::Colour::fromFloatRGBA (0.35f, 0.96f, 1.0f, 0.92f));
                    if (showTop)
                        g.strokePath (topFace, juce::PathStrokeType (2.4f));
                    if (showLeft)
                        g.strokePath (leftFace, juce::PathStrokeType (1.5f));
                    if (showRight)
                        g.strokePath (rightFace, juce::PathStrokeType (1.5f));
                }
    }

    auto makeFootprint = [&] (int x, int y, int z)
    {
        juce::Path path;
        path.startNewSubPath (projectIsometricPoint (x,     y,     z, boardArea));
        path.lineTo (projectIsometricPoint (x + 1, y,     z, boardArea));
        path.lineTo (projectIsometricPoint (x + 1, y + 1, z, boardArea));
        path.lineTo (projectIsometricPoint (x,     y + 1, z, boardArea));
        path.closeSubPath();
        return path;
    };

    auto makeFootprintCentre = [&] (int x, int y, int z)
    {
        const auto a = projectIsometricPoint (x,     y,     z, boardArea);
        const auto b = projectIsometricPoint (x + 1, y,     z, boardArea);
        const auto c = projectIsometricPoint (x + 1, y + 1, z, boardArea);
        const auto d = projectIsometricPoint (x,     y + 1, z, boardArea);

        return juce::Point<float> ((a.x + b.x + c.x + d.x) * 0.25f,
                                   (a.y + b.y + c.y + d.y) * 0.25f);
    };

    if (juce::isPositiveAndBelow (builderCursorX, getSurfaceWidth())
        && juce::isPositiveAndBelow (builderCursorY, getSurfaceDepth()))
    {
        const auto intervals = getActiveChordStackIntervals();
        int highestZ = builderLayer;
        for (const int interval : intervals)
            if (const int noteZ = builderLayer + interval; juce::isPositiveAndBelow (noteZ, getSurfaceHeight()))
                highestZ = juce::jmax (highestZ, noteZ);

        const int floorLayer = 0;
        const int baseLayer = juce::jmax (0, builderLayer);
        const int topLayer = highestZ + 1;
        auto floorFootprint = makeFootprint (builderCursorX, builderCursorY, floorLayer);
        auto baseIndicator = makeFootprint (builderCursorX, builderCursorY, baseLayer);
        auto indicator = makeFootprint (builderCursorX, builderCursorY, topLayer);
        const auto indicatorBounds = indicator.getBounds().expanded (10.0f + 4.0f * beatPulse);
        const auto floorCentre = makeFootprintCentre (builderCursorX, builderCursorY, floorLayer);
        const auto baseCentre = makeFootprintCentre (builderCursorX, builderCursorY, baseLayer);
        const auto topCentre = makeFootprintCentre (builderCursorX, builderCursorY, topLayer);

        juce::Path heightBeam;
        heightBeam.startNewSubPath (floorCentre);
        heightBeam.lineTo (baseCentre);
        heightBeam.lineTo (topCentre);

        g.setColour (juce::Colour::fromFloatRGBA (0.16f, 0.95f, 1.0f, 0.16f));
        g.fillPath (floorFootprint);
        g.setColour (juce::Colour::fromFloatRGBA (0.55f, 0.97f, 1.0f, 0.72f));
        g.strokePath (floorFootprint, juce::PathStrokeType (2.0f));

        if (baseLayer > 0)
        {
            g.setColour (juce::Colour::fromFloatRGBA (0.14f, 0.96f, 1.0f, 0.10f));
            g.fillPath (baseIndicator);
            g.setColour (juce::Colour::fromFloatRGBA (0.55f, 0.97f, 1.0f, 0.58f));
            g.strokePath (baseIndicator, juce::PathStrokeType (1.8f));
        }

        g.setColour (juce::Colour::fromFloatRGBA (0.22f, 0.96f, 1.0f, 0.28f + 0.16f * beatPulse));
        g.strokePath (heightBeam, juce::PathStrokeType (8.0f + 2.0f * beatPulse,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.92f));
        g.strokePath (heightBeam, juce::PathStrokeType (2.0f,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));

        g.setColour (juce::Colour::fromFloatRGBA (0.14f, 0.96f, 1.0f, 0.12f + 0.10f * beatPulse));
        g.fillEllipse (indicatorBounds);
        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 0.62f, 0.14f, 0.20f));
        g.fillPath (indicator);
        g.setColour (juce::Colour::fromFloatRGBA (0.10f, 0.95f, 1.0f, 0.96f));
        g.strokePath (indicator, juce::PathStrokeType (3.2f + beatPulse * 1.2f));
        g.setColour (juce::Colour::fromFloatRGBA (1.0f, 1.0f, 1.0f, 0.95f));
        g.strokePath (indicator, juce::PathStrokeType (1.2f));
    }
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 230));
    g.strokePath (footprintTopOutline, juce::PathStrokeType (1.6f));
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 92));
    g.strokePath (floorPlane, juce::PathStrokeType (1.1f));
}

void GameComponent::drawHotbar (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto bar = getHotbarBoundsForGridArea (area);
    auto chordBadge = bar.translated (0.0f, -38.0f).withHeight (28.0f);
    g.setColour (juce::Colour::fromRGBA (14, 22, 48, 238));
    g.fillRoundedRectangle (chordBadge, 8.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 22));
    g.fillRoundedRectangle (chordBadge.reduced (3.0f, 3.0f).withHeight (chordBadge.getHeight() * 0.34f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (124, 192, 255, 118));
    g.drawRoundedRectangle (chordBadge, 8.0f, 1.2f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.5f));
    g.drawText ("Chord  " + getIsometricChordName(), chordBadge.toNearestInt(), juce::Justification::centred);

    g.setColour (juce::Colour::fromRGBA (12, 18, 40, 238));
    g.fillRoundedRectangle (bar, 10.0f);
    g.setColour (juce::Colour::fromRGBA (255, 255, 255, 20));
    g.fillRoundedRectangle (bar.reduced (4.0f, 4.0f).withHeight (bar.getHeight() * 0.18f), 4.0f);
    g.setColour (juce::Colour::fromRGBA (124, 192, 255, 118));
    g.drawRoundedRectangle (bar, 10.0f, 1.3f);

    const std::array<juce::String, 4> labels { "x1", "x2", "x3", "x4" };
    for (int blockType = 1; blockType <= 4; ++blockType)
    {
        auto slot = getHotbarSlotBounds (area, blockType);
        const bool selected = isometricPlacementHeight == blockType;
        g.setColour (selected ? juce::Colour::fromRGBA (255, 188, 118, 98)
                              : juce::Colour::fromRGBA (18, 30, 58, 220));
        g.fillRoundedRectangle (slot, 8.0f);
        g.setColour (selected ? juce::Colour::fromRGBA (255, 228, 176, 220)
                              : juce::Colour::fromRGBA (104, 190, 255, 56));
        g.drawRoundedRectangle (slot, 8.0f, selected ? 1.8f : 1.2f);
        g.setColour (juce::Colour::fromRGBA (255, 255, 255, selected ? 30 : 16));
        g.fillRoundedRectangle (slot.reduced (3.0f, 3.0f).withHeight (slot.getHeight() * 0.20f), 3.0f);

        auto preview = slot.reduced (10.0f, 7.0f);
        auto swatch = preview.removeFromTop (preview.getHeight() * 0.56f);
        g.setColour (getNoteColourForLayer (juce::jlimit (0, getSurfaceHeight() - 1, builderLayer + blockType - 1)));
        g.fillRoundedRectangle (swatch, 4.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawRoundedRectangle (swatch, 4.0f, 1.0f);

        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (12.0f));
        g.drawText (labels[static_cast<size_t> (blockType - 1)],
                    preview.toNearestInt(), juce::Justification::centred);
    }
}

void GameComponent::drawTetrisBuildView (juce::Graphics& g, juce::Rectangle<float> area)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const auto board = getPerformanceBoardBounds (area);
    const float tileSize = board.getWidth() / static_cast<float> (getSurfaceWidth());
    const float pulse = 0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0032));
    const int activeLayerZ = juce::jlimit (1, getSurfaceHeight() - 1, tetrisBuildLayer);

    g.setColour (juce::Colour::fromRGBA (6, 11, 30, 236));
    g.fillRoundedRectangle (board.expanded (26.0f), 28.0f);
    g.setColour (juce::Colour::fromRGBA (74, 144, 255, 72));
    g.drawRoundedRectangle (board.expanded (26.0f), 28.0f, 1.8f);

    juce::ColourGradient boardGlow (juce::Colour::fromRGBA (42, 102, 212, 102),
                                    board.getCentreX(), board.getCentreY(),
                                    juce::Colour::fromRGBA (8, 13, 36, 0),
                                    board.getCentreX(), board.getBottom() + 80.0f,
                                    true);
    g.setGradientFill (boardGlow);
    g.fillEllipse (board.expanded (96.0f, 82.0f));

    for (int y = 0; y < getSurfaceDepth(); ++y)
    {
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            auto cell = juce::Rectangle<float> (board.getX() + static_cast<float> (x) * tileSize,
                                                board.getY() + static_cast<float> (y) * tileSize,
                                                tileSize,
                                                tileSize);
            g.setColour (juce::Colour::fromRGBA (8, 14, 34, 214));
            g.fillRect (cell);

            struct LayerSlice { juce::Colour colour; bool active = false; };
            std::vector<LayerSlice> slices;
            for (int z = 1; z < getSurfaceHeight(); ++z)
                if (activePlanetState->getBlock (x, y, z) != 0)
                    slices.push_back ({ getNoteColourForLayer (z), z == activeLayerZ });

            if (! slices.empty())
            {
                auto innerCell = cell.reduced (2.0f);
                const float sliceHeight = innerCell.getHeight() / static_cast<float> (slices.size());
                for (size_t i = 0; i < slices.size(); ++i)
                {
                    auto sliceBounds = juce::Rectangle<float> (innerCell.getX(),
                                                               innerCell.getY() + sliceHeight * static_cast<float> (i),
                                                               innerCell.getWidth(),
                                                               juce::jmax (1.0f, sliceHeight + 0.5f));
                    auto colour = slices[i].colour;
                    if (slices[i].active)
                        colour = colour.withMultipliedBrightness (1.12f);
                    else
                        colour = colour.interpolatedWith (juce::Colour::greyLevel (colour.getPerceivedBrightness()), 0.88f)
                                       .withMultipliedBrightness (0.52f)
                                       .withMultipliedAlpha (0.55f);
                    g.setColour (colour);
                    g.fillRect (sliceBounds);
                    if (slices[i].active)
                    {
                        g.setColour (juce::Colours::white.withAlpha (0.18f));
                        g.drawRect (sliceBounds, 1.0f);
                    }
                }
            }

            g.setColour (juce::Colour::fromRGBA (88, 122, 214, 32));
            g.drawRect (cell, 1.0f);
        }
    }

    if (tetrisPiece.active)
    {
        const bool pieceFits = tetrisPieceFits (tetrisPiece) && ! tetrisPieceCollidesWithVoxels (tetrisPiece);
        const auto previewColour = pieceFits
                                     ? juce::Colour::fromRGBA (84, 238, 255, 188)
                                     : juce::Colour::fromRGBA (255, 116, 116, 188);
        for (const auto& placementCell : getTetrisPlacementCells (tetrisPiece))
        {
            if (! juce::isPositiveAndBelow (placementCell.x, getSurfaceWidth()) || ! juce::isPositiveAndBelow (placementCell.y, getSurfaceDepth()))
                continue;

            auto cell = juce::Rectangle<float> (board.getX() + static_cast<float> (placementCell.x) * tileSize,
                                                board.getY() + static_cast<float> (placementCell.y) * tileSize,
                                                tileSize,
                                                tileSize).reduced (1.6f);
            g.setColour (previewColour.withAlpha (0.22f + 0.08f * pulse));
            g.fillRoundedRectangle (cell, 5.0f);
            g.setColour (previewColour);
            g.drawRoundedRectangle (cell, 5.0f, 2.0f);
        }
    }

    auto infoTag = juce::Rectangle<float> (248.0f, 28.0f).withCentre ({ board.getCentreX(), board.getY() - 24.0f });
    g.setColour (juce::Colour::fromRGBA (8, 14, 34, 228));
    g.fillRoundedRectangle (infoTag, 8.0f);
    g.setColour (juce::Colour::fromRGBA (84, 238, 255, 160));
    g.drawRoundedRectangle (infoTag, 8.0f, 1.5f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f));
    g.drawFittedText ("Tetris  " + getTetrominoTypeName (tetrisPiece.active ? tetrisPiece.type : nextTetrisType)
                      + "  layer " + juce::String (activeLayerZ),
                      infoTag.toNearestInt(),
                      juce::Justification::centred,
                      1);
}

void GameComponent::drawAutomataBuildView (juce::Graphics& g, juce::Rectangle<float> area)
{
    ensureActivePlanetLoaded();
    if (activePlanetState == nullptr)
        return;

    const auto board = getPerformanceBoardBounds (area);
    const float tileSize = board.getWidth() / static_cast<float> (getSurfaceWidth());
    const float pulse = 0.5f + 0.5f * static_cast<float> (std::sin (juce::Time::getMillisecondCounterHiRes() * 0.0032));
    const int activeLayerZ = juce::jlimit (1, getSurfaceHeight() - 1, automataBuildLayer);

    g.setColour (juce::Colour::fromRGBA (6, 11, 30, 236));
    g.fillRoundedRectangle (board.expanded (26.0f), 28.0f);
    g.setColour (juce::Colour::fromRGBA (74, 144, 255, 72));
    g.drawRoundedRectangle (board.expanded (26.0f), 28.0f, 1.8f);

    juce::ColourGradient boardGlow (juce::Colour::fromRGBA (42, 102, 212, 102),
                                    board.getCentreX(), board.getCentreY(),
                                    juce::Colour::fromRGBA (8, 13, 36, 0),
                                    board.getCentreX(), board.getBottom() + 80.0f,
                                    true);
    g.setGradientFill (boardGlow);
    g.fillEllipse (board.expanded (96.0f, 82.0f));

    for (int y = 0; y < getSurfaceDepth(); ++y)
    {
        for (int x = 0; x < getSurfaceWidth(); ++x)
        {
            auto cell = juce::Rectangle<float> (board.getX() + static_cast<float> (x) * tileSize,
                                                board.getY() + static_cast<float> (y) * tileSize,
                                                tileSize,
                                                tileSize);
            g.setColour (juce::Colour::fromRGBA (8, 14, 34, 214));
            g.fillRect (cell);

            const bool activeFilled = activePlanetState->getBlock (x, y, activeLayerZ) != 0;
            int otherLayerCount = 0;
            for (int z = 1; z < getSurfaceHeight(); ++z)
                if (z != activeLayerZ && activePlanetState->getBlock (x, y, z) != 0)
                    ++otherLayerCount;

            auto innerCell = cell.reduced (2.0f);
            if (otherLayerCount > 0)
            {
                const float dimInset = juce::jmin (5.0f, 1.6f + 0.4f * static_cast<float> (otherLayerCount - 1));
                auto dimmedCell = innerCell.reduced (dimInset);
                g.setColour (juce::Colour::fromRGBA (130, 144, 176, static_cast<uint8_t> (46 + juce::jmin (otherLayerCount, 4) * 20)));
                g.fillRoundedRectangle (dimmedCell, 5.0f);
                g.setColour (juce::Colour::fromRGBA (196, 208, 236, static_cast<uint8_t> (18 + juce::jmin (otherLayerCount, 4) * 10)));
                g.drawRoundedRectangle (dimmedCell, 5.0f, 1.0f);
            }

            if (activeFilled)
            {
                const auto activeColour = getNoteColourForLayer (activeLayerZ).withMultipliedBrightness (1.1f);
                g.setColour (activeColour.withAlpha (0.22f + 0.08f * pulse));
                g.fillRoundedRectangle (innerCell.expanded (3.0f), 7.0f);
                g.setColour (activeColour);
                g.fillRoundedRectangle (innerCell, 5.0f);
                g.setColour (juce::Colours::white.withAlpha (0.30f));
                g.drawRoundedRectangle (innerCell, 5.0f, 1.2f);
            }

            if (automataHoverCell.has_value() && automataHoverCell->x == x && automataHoverCell->y == y)
            {
                auto hoverCell = cell.reduced (1.4f);
                g.setColour (juce::Colour::fromRGBA (84, 238, 255, static_cast<uint8_t> (38 + 24 * pulse)));
                g.fillRoundedRectangle (hoverCell.expanded (4.0f), 7.0f);
                g.setColour (juce::Colour::fromRGBA (84, 238, 255, 220));
                g.drawRoundedRectangle (hoverCell, 7.0f, 2.2f);
            }

            g.setColour (juce::Colour::fromRGBA (88, 122, 214, 32));
            g.drawRect (cell, 1.0f);
        }
    }

    auto infoTag = juce::Rectangle<float> (272.0f, 28.0f).withCentre ({ board.getCentreX(), board.getY() - 24.0f });
    g.setColour (juce::Colour::fromRGBA (8, 14, 34, 228));
    g.fillRoundedRectangle (infoTag, 8.0f);
    g.setColour (juce::Colour::fromRGBA (84, 238, 255, 160));
    g.drawRoundedRectangle (infoTag, 8.0f, 1.5f);
    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (13.0f));
    const juce::String ruleName = ((activeLayerZ % 4) == 1) ? "Coral"
                                 : ((activeLayerZ % 4) == 2) ? "Fredkin"
                                 : ((activeLayerZ % 4) == 3) ? "DayNight"
                                                             : "Life";
    g.drawFittedText (ruleName + "  layer " + juce::String (activeLayerZ),
                      infoTag.toNearestInt(),
                      juce::Justification::centred,
                      1);
}

void GameComponent::drawFirstPersonBuilder (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto target = findFirstPersonTarget();
    juce::ColourGradient skyGradient (juce::Colour (0xff726859), static_cast<float> (area.getCentreX()), static_cast<float> (area.getY()),
                                      juce::Colour (0xff2c241e), static_cast<float> (area.getCentreX()), static_cast<float> (area.getBottom()), false);
    skyGradient.addColour (0.58, juce::Colour (0xff51463a));
    g.setGradientFill (skyGradient);
    g.fillRect (area);

    auto lowerBand = area.withTrimmedTop (static_cast<int> (area.getHeight() * 0.58f));
    juce::ColourGradient groundFade (juce::Colour (0x00000000), static_cast<float> (lowerBand.getCentreX()), static_cast<float> (lowerBand.getY()),
                                     juce::Colour (0xaa100c09), static_cast<float> (lowerBand.getCentreX()), static_cast<float> (lowerBand.getBottom()), false);
    g.setGradientFill (groundFade);
    g.fillRect (lowerBand);

    const float centreX = static_cast<float> (area.getCentreX());
    const float centreY = static_cast<float> (area.getCentreY());
    const float projectionScale = static_cast<float> (area.getWidth()) * 0.9f;
    const float camX = firstPersonState.x;
    const float camY = firstPersonState.y;
    const float camZ = firstPersonState.eyeZ;
    const float cosYaw = std::cos (firstPersonState.yaw);
    const float sinYaw = std::sin (firstPersonState.yaw);
    const float cosPitch = std::cos (firstPersonState.pitch);
    const float sinPitch = std::sin (firstPersonState.pitch);

    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
    };

    struct FaceDraw
    {
        juce::Path path;
        std::array<Vec3, 4> corners;
        float depth = 0.0f;
        int blockType = 0;
        int zLayer = 0;
        float brightness = 1.0f;
        bool topFace = false;
        bool bottomFace = false;
        bool targeted = false;
    };

    std::vector<FaceDraw> faces;
    faces.reserve (512);

    auto projectPoint = [&] (float wx, float wy, float wz, juce::Point<float>& out) -> std::optional<float>
    {
        const float relX = wx - camX;
        const float relY = wy - camY;
        const float relZ = wz - camZ;
        const float yawX = relX * cosYaw - relY * sinYaw;
        const float yawY = relX * sinYaw + relY * cosYaw;
        const float camSpaceY = relZ * cosPitch - yawY * sinPitch;
        const float depth = relZ * sinPitch + yawY * cosPitch;
        if (depth <= 0.05f)
            return std::nullopt;

        out.x = centreX + (yawX / depth) * projectionScale;
        out.y = centreY - (camSpaceY / depth) * projectionScale;
        return depth;
    };

    auto addFace = [&] (const std::array<Vec3, 4>& corners, int blockType, int zLayer, float brightness, bool topFace, bool bottomFace, bool targetedFace)
    {
        FaceDraw face;
        face.corners = corners;
        face.blockType = blockType;
        face.zLayer = zLayer;
        face.brightness = brightness;
        face.topFace = topFace;
        face.bottomFace = bottomFace;
        face.targeted = targetedFace;

        bool first = true;
        float depthSum = 0.0f;
        for (const auto& corner : corners)
        {
            juce::Point<float> projected;
            auto depth = projectPoint (corner.x, corner.y, corner.z, projected);
            if (! depth.has_value())
                return;

            depthSum += *depth;
            if (first)
            {
                face.path.startNewSubPath (projected);
                first = false;
            }
            else
            {
                face.path.lineTo (projected);
            }
        }

        face.path.closeSubPath();
        face.depth = depthSum * 0.25f;
        faces.push_back (std::move (face));
    };

    for (int z = 0; z < getSurfaceHeight(); ++z)
    {
        for (int y = 0; y < getSurfaceDepth(); ++y)
        {
            for (int x = 0; x < getSurfaceWidth(); ++x)
            {
                const auto block = activePlanetState->getBlock (x, y, z);
                if (block == 0)
                    continue;

                const float distance = std::abs (static_cast<float> (x) - camX)
                                     + std::abs (static_cast<float> (y) - camY)
                                     + std::abs (static_cast<float> (z) - camZ);
                if (distance > 33.0f)
                    continue;

                const bool targetedBlock = target.valid && x == target.hitX && y == target.hitY && z == target.hitZ;

                auto emptyAt = [&] (int nx, int ny, int nz)
                {
                    return ! juce::isPositiveAndBelow (nx, getSurfaceWidth())
                        || ! juce::isPositiveAndBelow (ny, getSurfaceDepth())
                        || ! juce::isPositiveAndBelow (nz, getSurfaceHeight())
                        || activePlanetState->getBlock (nx, ny, nz) == 0;
                };

                if (emptyAt (x, y, z + 1))
                {
                    addFace ({ Vec3 { static_cast<float> (x),     static_cast<float> (y),     static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z + 1) } },
                             block, z, 1.0f, true, false, targetedBlock);
                }

                if (emptyAt (x, y, z - 1))
                {
                    addFace ({ Vec3 { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z) },
                               Vec3 { static_cast<float> (x),     static_cast<float> (y),     static_cast<float> (z) } },
                             block, z, 0.48f, false, true, targetedBlock);
                }

                if (emptyAt (x, y - 1, z))
                    addFace ({ Vec3 { static_cast<float> (x),     static_cast<float> (y), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y), static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x),     static_cast<float> (y), static_cast<float> (z + 1) } },
                             block, z, 0.82f, false, false, targetedBlock);
                if (emptyAt (x + 1, y, z))
                    addFace ({ Vec3 { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z + 1) } },
                             block, z, 0.62f, false, false, targetedBlock);
                if (emptyAt (x, y + 1, z))
                    addFace ({ Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z + 1) } },
                             block, z, 0.82f, false, false, targetedBlock);
                if (emptyAt (x - 1, y, z))
                    addFace ({ Vec3 { static_cast<float> (x), static_cast<float> (y + 1), static_cast<float> (z) },
                               Vec3 { static_cast<float> (x), static_cast<float> (y),     static_cast<float> (z) },
                               Vec3 { static_cast<float> (x), static_cast<float> (y),     static_cast<float> (z + 1) },
                               Vec3 { static_cast<float> (x), static_cast<float> (y + 1), static_cast<float> (z + 1) } },
                             block, z, 0.62f, false, false, targetedBlock);
            }
        }
    }

    std::sort (faces.begin(), faces.end(), [] (const FaceDraw& a, const FaceDraw& b) { return a.depth > b.depth; });

    auto drawStableFirstPersonFace = [&] (const FaceDraw& face)
    {
        const auto noteBase = getNoteColourForLayer (face.zLayer);
        const auto materialTint = getBlockColour (face.blockType);
        const auto base = noteBase.interpolatedWith (materialTint, 0.18f).withMultipliedBrightness (face.brightness);

        auto sampleColour = [&] (int uIndex, int vIndex)
        {
            const int hash = (uIndex * 92821) ^ (vIndex * 68917) ^ (face.blockType * 101) ^ (face.zLayer * 53);
            auto sample = base;

            if (face.blockType == 1)
                sample = sample.withMultipliedBrightness ((hash & 1) == 0 ? 0.82f : 1.08f);
            else if (face.blockType == 2)
                sample = sample.interpolatedWith (juce::Colours::white, (hash & 3) == 0 ? 0.18f : 0.05f);
            else if (face.blockType == 3)
                sample = sample.withMultipliedBrightness ((hash & 2) == 0 ? 0.90f : 1.12f);
            else if (face.blockType == 4)
                sample = sample.interpolatedWith (juce::Colours::darkgreen, (hash & 1) == 0 ? 0.18f : 0.05f);

            return sample;
        };

        auto bilerp = [] (const Vec3& a, const Vec3& b, const Vec3& c, const Vec3& d, float u, float v)
        {
            const auto lerpVec = [] (const Vec3& p0, const Vec3& p1, float t)
            {
                return Vec3 { p0.x + (p1.x - p0.x) * t,
                              p0.y + (p1.y - p0.y) * t,
                              p0.z + (p1.z - p0.z) * t };
            };

            const auto ab = lerpVec (a, b, u);
            const auto dc = lerpVec (d, c, u);
            return lerpVec (ab, dc, v);
        };

        constexpr int subdivisions = 6;
        for (int vy = 0; vy < subdivisions; ++vy)
        {
            const float v0 = static_cast<float> (vy) / static_cast<float> (subdivisions);
            const float v1 = static_cast<float> (vy + 1) / static_cast<float> (subdivisions);

            for (int ux = 0; ux < subdivisions; ++ux)
            {
                const float u0 = static_cast<float> (ux) / static_cast<float> (subdivisions);
                const float u1 = static_cast<float> (ux + 1) / static_cast<float> (subdivisions);

                const auto p00 = bilerp (face.corners[0], face.corners[1], face.corners[2], face.corners[3], u0, v0);
                const auto p10 = bilerp (face.corners[0], face.corners[1], face.corners[2], face.corners[3], u1, v0);
                const auto p11 = bilerp (face.corners[0], face.corners[1], face.corners[2], face.corners[3], u1, v1);
                const auto p01 = bilerp (face.corners[0], face.corners[1], face.corners[2], face.corners[3], u0, v1);

                juce::Point<float> q00, q10, q11, q01;
                if (! projectPoint (p00.x, p00.y, p00.z, q00).has_value()
                    || ! projectPoint (p10.x, p10.y, p10.z, q10).has_value()
                    || ! projectPoint (p11.x, p11.y, p11.z, q11).has_value()
                    || ! projectPoint (p01.x, p01.y, p01.z, q01).has_value())
                    continue;

                juce::Path quad;
                quad.startNewSubPath (q00);
                quad.lineTo (q10);
                quad.lineTo (q11);
                quad.lineTo (q01);
                quad.closeSubPath();

                g.setColour (sampleColour (ux, vy));
                g.fillPath (quad);
            }
        }

        g.setColour (juce::Colours::black.withAlpha (face.topFace ? 0.05f : (face.bottomFace ? 0.18f : 0.14f)));
        g.strokePath (face.path, juce::PathStrokeType (1.0f));
    };

    for (const auto& face : faces)
    {
        drawStableFirstPersonFace (face);

        if (face.targeted)
        {
            g.setColour (juce::Colour (0xfffff0d6));
            g.strokePath (face.path, juce::PathStrokeType (2.0f));
        }
    }

    auto drawWireBox = [&] (int x, int y, int z, juce::Colour colour)
    {
        const std::array<Vec3, 8> corners {{
            { static_cast<float> (x),     static_cast<float> (y),     static_cast<float> (z) },
            { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z) },
            { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z) },
            { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z) },
            { static_cast<float> (x),     static_cast<float> (y),     static_cast<float> (z + 1) },
            { static_cast<float> (x + 1), static_cast<float> (y),     static_cast<float> (z + 1) },
            { static_cast<float> (x + 1), static_cast<float> (y + 1), static_cast<float> (z + 1) },
            { static_cast<float> (x),     static_cast<float> (y + 1), static_cast<float> (z + 1) }
        }};

        std::array<juce::Point<float>, 8> projected;
        for (size_t i = 0; i < corners.size(); ++i)
            if (! projectPoint (corners[i].x, corners[i].y, corners[i].z, projected[i]).has_value())
                return;

        const std::array<std::pair<int, int>, 12> edges {{
            { 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
            { 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
            { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }
        }};

        g.setColour (colour.withAlpha (0.22f));
        juce::Path fillPath;
        fillPath.startNewSubPath (projected[4]);
        fillPath.lineTo (projected[5]);
        fillPath.lineTo (projected[6]);
        fillPath.lineTo (projected[7]);
        fillPath.closeSubPath();
        g.fillPath (fillPath);

        g.setColour (colour);
        for (const auto& edge : edges)
            g.drawLine ({ projected[static_cast<size_t> (edge.first)], projected[static_cast<size_t> (edge.second)] }, 1.6f);
    };

    if (target.valid
        && juce::isPositiveAndBelow (target.placeX, getSurfaceWidth())
        && juce::isPositiveAndBelow (target.placeY, getSurfaceDepth())
        && juce::isPositiveAndBelow (target.placeZ, getSurfaceHeight()))
        drawWireBox (target.placeX, target.placeY, target.placeZ, juce::Colour::fromFloatRGBA (0.14f, 0.95f, 1.0f, 0.92f));

    drawHotbar (g, area);

    const float crosshairPulse = 0.55f + 0.45f * std::sin (transportPhase * juce::MathConstants<float>::twoPi * 8.0f);
    const float crosshairSize = 8.0f + crosshairPulse * 2.0f;
    g.setColour (juce::Colours::white);
    g.drawLine (centreX - crosshairSize, centreY,
                centreX + crosshairSize, centreY, 1.6f);
    g.drawLine (centreX, centreY - crosshairSize,
                centreX, centreY + crosshairSize, 1.6f);
}

void GameComponent::drawTexturedCube (juce::Graphics& g, juce::Point<float> origin, float halfWidth, float halfHeight, int blockType, bool selected) const
{
    juce::Path top;
    top.startNewSubPath (origin.x, origin.y - halfHeight);
    top.lineTo (origin.x + halfWidth, origin.y);
    top.lineTo (origin.x, origin.y + halfHeight);
    top.lineTo (origin.x - halfWidth, origin.y);
    top.closeSubPath();

    juce::Path left;
    left.startNewSubPath (origin.x - halfWidth, origin.y);
    left.lineTo (origin.x, origin.y + halfHeight);
    left.lineTo (origin.x, origin.y + halfHeight + halfWidth);
    left.lineTo (origin.x - halfWidth, origin.y + halfWidth);
    left.closeSubPath();

    juce::Path right;
    right.startNewSubPath (origin.x + halfWidth, origin.y);
    right.lineTo (origin.x, origin.y + halfHeight);
    right.lineTo (origin.x, origin.y + halfHeight + halfWidth);
    right.lineTo (origin.x + halfWidth, origin.y + halfWidth);
    right.closeSubPath();

    fillTexturedDiamond (g, right, blockType, builderLayer, 0.62f, false);
    fillTexturedDiamond (g, left, blockType, builderLayer, 0.46f, false);
    fillTexturedDiamond (g, top, blockType, builderLayer, 1.0f, true);

    g.setColour (juce::Colour (0x40000000));
    g.strokePath (left, juce::PathStrokeType (1.0f));
    g.strokePath (right, juce::PathStrokeType (1.0f));
    g.strokePath (top, juce::PathStrokeType (1.0f));

    if (selected)
    {
        fillGlow (g, juce::Rectangle<float> (origin.x - halfWidth * 1.8f, origin.y - halfHeight * 2.0f,
                                             halfWidth * 3.6f, halfWidth * 3.2f),
                  juce::Colour (0xffffd298), 0.20f);
        g.setColour (juce::Colour (0xfffff0dc));
        g.strokePath (top, juce::PathStrokeType (2.0f));
        g.strokePath (left, juce::PathStrokeType (1.5f));
        g.strokePath (right, juce::PathStrokeType (1.5f));
    }
}

void GameComponent::fillTexturedDiamond (juce::Graphics& g, const juce::Path& path, int blockType, int zLayer, float brightness, bool topFace) const
{
    auto bounds = path.getBounds();
    g.saveState();
    g.reduceClipRegion (path, {});
    fillTexturedRect (g, bounds, blockType, zLayer, brightness, topFace);
    g.setColour (juce::Colours::black.withAlpha (topFace ? 0.0f : 0.14f));
    if (! topFace)
        g.fillRect (bounds.removeFromBottom (bounds.getHeight() * 0.25f));
    g.restoreState();
}

void GameComponent::fillTexturedRect (juce::Graphics& g, juce::Rectangle<float> area, int blockType, int zLayer, float brightness, bool topFace) const
{
    const auto noteBase = getNoteColourForLayer (zLayer);
    const auto materialTint = getBlockColour (blockType);
    const auto base = noteBase.interpolatedWith (materialTint, 0.18f).withMultipliedBrightness (brightness);
    g.setColour (base);
    g.fillRect (area);

    g.saveState();
    g.reduceClipRegion (area.toNearestInt());

    const float tile = juce::jmax (3.0f, area.getWidth() / 6.0f);
    for (float y = area.getY(); y < area.getBottom(); y += tile)
    {
        for (float x = area.getX(); x < area.getRight(); x += tile)
        {
            const int hash = (static_cast<int> (x * 10.0f) * 31) ^ (static_cast<int> (y * 10.0f) * 17) ^ (blockType * 101);
            auto sample = base;

            if (blockType == 1)
                sample = sample.withMultipliedBrightness ((hash & 1) == 0 ? 0.82f : 1.08f);
            else if (blockType == 2)
                sample = sample.interpolatedWith (juce::Colours::white, (hash & 3) == 0 ? 0.18f : 0.05f);
            else if (blockType == 3)
                sample = sample.withMultipliedBrightness ((hash & 2) == 0 ? 0.90f : 1.12f);
            else if (blockType == 4)
                sample = sample.interpolatedWith (juce::Colours::darkgreen, (hash & 1) == 0 ? 0.18f : 0.05f);

            g.setColour (sample);
            g.fillRect (juce::Rectangle<float> (x, y, tile + 0.6f, tile + 0.6f));
        }
    }

    if (topFace)
    {
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        for (float x = area.getX(); x < area.getRight(); x += tile * 2.0f)
            g.drawLine (x, area.getY(), x + area.getHeight(), area.getBottom(), 1.0f);
    }
    else
    {
        g.setColour (juce::Colours::black.withAlpha (0.12f));
        for (float y = area.getY(); y < area.getBottom(); y += tile * 1.7f)
            g.drawLine (area.getX(), y, area.getRight(), y, 1.0f);
    }

    g.restoreState();
}

juce::Colour GameComponent::getBlockColour (int blockType) const
{
    switch (blockType)
    {
        case 1: return juce::Colour (0xff5d4b3b);
        case 2: return juce::Colour (0xff7ab6c8);
        case 3: return juce::Colour (0xffd39a52);
        case 4: return juce::Colour (0xff597b4b);
        default: return juce::Colour (0xff0c0908);
    }
}

juce::Colour GameComponent::getNoteColourForLayer (int zLayer) const
{
    if (zLayer <= 0)
        return juce::Colour::fromRGB (72, 76, 88);

    switch ((zLayer - 1) % 12)
    {
        case 0: return juce::Colour::fromRGB (247, 223, 67);
        case 1: return juce::Colour::fromRGB (240, 147, 49);
        case 2: return juce::Colour::fromRGB (78, 191, 104);
        case 3: return juce::Colour::fromRGB (61, 210, 201);
        case 4: return juce::Colour::fromRGB (138, 93, 214);
        case 5: return juce::Colour::fromRGB (229, 78, 196);
        case 6: return juce::Colour::fromRGB (232, 60, 56);
        case 7: return juce::Colour::fromRGB (238, 100, 46);
        case 8: return juce::Colour::fromRGB (214, 47, 47);
        case 9: return juce::Colour::fromRGB (244, 155, 57);
        case 10: return juce::Colour::fromRGB (248, 194, 70);
        case 11: return juce::Colour::fromRGB (247, 173, 84);
        default: break;
    }

    return juce::Colours::white;
}

juce::String GameComponent::getNoteNameForLayer (int zLayer) const
{
    if (zLayer <= 0)
        return "No note";

    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    return names[static_cast<size_t> ((zLayer - 1) % 12)];
}

void GameComponent::stepPerformanceAgents()
{
    switch (performanceAgentMode)
    {
        case PerformanceAgentMode::snakes:
        case PerformanceAgentMode::trains:
            stepPerformanceSnakes();
            break;
        case PerformanceAgentMode::orbiters:
            stepPerformanceOrbiters();
            break;
        case PerformanceAgentMode::automata:
            stepPerformanceAutomata();
            break;
        case PerformanceAgentMode::ripple:
            stepPerformanceRipples();
            break;
        case PerformanceAgentMode::sequencer:
            stepPerformanceSequencers();
            break;
        case PerformanceAgentMode::tenori:
            stepPerformanceTenori();
            break;
    }
}

void GameComponent::stepPerformanceSnakes()
{
    const auto bounds = getPerformanceRegionBounds();
    static const std::array<juce::Point<int>, 4> snakeDirections { juce::Point<int> { 1, 0 }, juce::Point<int> { -1, 0 },
                                                                   juce::Point<int> { 0, 1 }, juce::Point<int> { 0, -1 } };

    auto inside = [&] (juce::Point<int> p) { return bounds.contains (p); };
    auto occupiedByAnySnake = [&] (juce::Point<int> cell, int movingSnakeIndex)
    {
        for (int i = 0; i < static_cast<int> (performanceSnakes.size()); ++i)
        {
            const auto& other = performanceSnakes[static_cast<size_t> (i)];
            for (size_t segment = 0; segment < other.body.size(); ++segment)
            {
                if (i == movingSnakeIndex && segment == other.body.size() - 1)
                    continue;
                if (other.body[segment] == cell)
                    return true;
            }
        }

        return false;
    };

    for (int snakeIndex = 0; snakeIndex < static_cast<int> (performanceSnakes.size()); ++snakeIndex)
    {
        auto& snake = performanceSnakes[static_cast<size_t> (snakeIndex)];
        if (snake.body.empty())
            continue;

        if (performanceAgentMode == PerformanceAgentMode::trains)
        {
            const auto head = snake.body.front();
            std::vector<juce::Point<int>> trackOptions;
            for (const auto& dir : snakeDirections)
            {
                const auto candidate = head + dir;
                if (! inside (candidate) || occupiedByAnySnake (candidate, snakeIndex))
                    continue;
                if (hasPerformanceTrackAt (candidate))
                    trackOptions.push_back (dir);
            }

            if (! trackOptions.empty())
            {
                const auto switchDisc = std::find_if (performanceDiscs.begin(), performanceDiscs.end(),
                                                      [head] (const PerformanceDisc& disc) { return disc.cell == head; });
                if (switchDisc != performanceDiscs.end()
                    && std::find (trackOptions.begin(), trackOptions.end(), switchDisc->direction) != trackOptions.end())
                {
                    snake.direction = switchDisc->direction;
                    performanceFlashes.push_back ({ head, juce::Colour::fromRGBA (255, 208, 112, 255), 0.72f, true });
                }
                else if (std::find (trackOptions.begin(), trackOptions.end(), snake.direction) == trackOptions.end())
                {
                    snake.direction = trackOptions.front();
                }
            }
        }

        auto nextHead = snake.body.front() + snake.direction;
        if (! inside (nextHead) || occupiedByAnySnake (nextHead, snakeIndex))
        {
            if (snake.body.size() >= 2)
            {
                std::reverse (snake.body.begin(), snake.body.end());
                snake.direction = snake.body.front() - snake.body[1];
            }
            else
            {
                snake.direction = { -snake.direction.x, -snake.direction.y };
            }

            nextHead = snake.body.front() + snake.direction;
        }

        if (! inside (nextHead) || occupiedByAnySnake (nextHead, snakeIndex))
            continue;

        if (const auto disc = std::find_if (performanceDiscs.begin(), performanceDiscs.end(),
                                            [nextHead] (const PerformanceDisc& reflector) { return reflector.cell == nextHead; });
            disc != performanceDiscs.end())
        {
            snake.direction = disc->direction;
            performanceFlashes.push_back ({ nextHead, juce::Colour::fromRGBA (255, 196, 96, 255), 1.0f, true });
        }

        snake.body.insert (snake.body.begin(), nextHead);
        if (static_cast<int> (snake.body.size()) > getPerformanceSnakeLength())
            snake.body.pop_back();

        if (snakeTriggerMode == SnakeTriggerMode::headOnly)
        {
            recordPerformanceMovementCell (nextHead);
            triggerPerformanceNotesAtCell (nextHead);
        }
        else
        {
            for (const auto& segment : snake.body)
            {
                recordPerformanceMovementCell (segment);
                triggerPerformanceNotesAtCell (segment);
            }
        }
    }
}

void GameComponent::stepPerformanceOrbiters()
{
    const auto bounds = getPerformanceRegionBounds();
    if (performanceOrbitCenters.empty())
        performanceOrbitCenters.push_back (bounds.getCentre());

    auto inside = [&] (juce::Point<int> p) { return bounds.contains (p); };
    auto axisDir = [] (juce::Point<int> vector)
    {
        if (std::abs (vector.x) >= std::abs (vector.y))
            return juce::Point<int> (vector.x < 0 ? -1 : 1, 0);
        return juce::Point<int> (0, vector.y < 0 ? -1 : 1);
    };

    for (auto& orbiter : performanceSnakes)
    {
        if (orbiter.body.empty())
            continue;

        const auto centre = performanceOrbitCenters[static_cast<size_t> (orbiter.orbitIndex % static_cast<int> (performanceOrbitCenters.size()))];
        const auto head = orbiter.body.front();
        const auto delta = head - centre;
        const int desiredRadius = 3 + (orbiter.orbitIndex % 4);
        const int distanceSq = delta.x * delta.x + delta.y * delta.y;
        const juce::Point<int> tangent = orbiter.clockwise ? juce::Point<int> (delta.y, -delta.x)
                                                           : juce::Point<int> (-delta.y, delta.x);
        juce::Point<int> direction = axisDir ((tangent.x == 0 && tangent.y == 0) ? juce::Point<int> (1, 0) : tangent);
        if (distanceSq < desiredRadius * desiredRadius)
            direction = axisDir ((delta.x == 0 && delta.y == 0) ? juce::Point<int> (1, 0) : delta);
        else if (distanceSq > (desiredRadius + 1) * (desiredRadius + 1))
            direction = axisDir ({ -delta.x, -delta.y });

        auto next = head + direction;
        if (! inside (next))
        {
            orbiter.clockwise = ! orbiter.clockwise;
            const auto altTangent = orbiter.clockwise ? juce::Point<int> (delta.y, -delta.x)
                                                      : juce::Point<int> (-delta.y, delta.x);
            direction = axisDir ((altTangent.x == 0 && altTangent.y == 0) ? juce::Point<int> (1, 0) : altTangent);
            next = head + direction;
        }

        if (! inside (next))
            continue;

        orbiter.direction = direction;
        orbiter.body[0] = next;
        recordPerformanceMovementCell (next);
        triggerPerformanceNotesAtCell (next);
    }
}

void GameComponent::stepPerformanceAutomata()
{
    const auto bounds = getPerformanceRegionBounds();
    if (performanceAutomataCells.empty())
    {
        resetPerformanceAgents();
        if (performanceAutomataCells.empty())
            return;
    }

    std::unordered_map<int, int> neighbourCounts;
    auto keyFor = [] (juce::Point<int> cell) { return (cell.y << 16) ^ cell.x; };
    auto cellFor = [] (int key) { return juce::Point<int> (key & 0xffff, key >> 16); };

    for (const auto& cell : performanceAutomataCells)
    {
        for (int dy = -1; dy <= 1; ++dy)
            for (int dx = -1; dx <= 1; ++dx)
            {
                if (dx == 0 && dy == 0)
                    continue;
                const juce::Point<int> n { cell.x + dx, cell.y + dy };
                if (bounds.contains (n))
                    ++neighbourCounts[keyFor (n)];
            }
    }

    std::vector<juce::Point<int>> nextCells;
    for (const auto& [key, count] : neighbourCounts)
    {
        const auto cell = cellFor (key);
        const bool alive = std::find (performanceAutomataCells.begin(), performanceAutomataCells.end(), cell) != performanceAutomataCells.end();
        if ((alive && (count == 2 || count == 3)) || (! alive && count == 3))
            nextCells.push_back (cell);
    }

    if (nextCells.empty())
    {
        resetPerformanceAgents();
        return;
    }

    performanceAutomataCells = nextCells;
    for (const auto& cell : performanceAutomataCells)
    {
        recordPerformanceMovementCell (cell);
        triggerPerformanceNotesAtCell (cell);
    }
}

void GameComponent::stepPerformanceRipples()
{
    const auto bounds = getPerformanceRegionBounds();
    if (performanceRipples.empty())
    {
        resetPerformanceAgents();
        if (performanceRipples.empty())
            return;
    }

    juce::Random rng;
    for (auto& ripple : performanceRipples)
    {
        const int radius = ripple.radius;
        for (int dy = -radius; dy <= radius; ++dy)
            for (int dx = -radius; dx <= radius; ++dx)
            {
                const int manhattan = std::abs (dx) + std::abs (dy);
                if (manhattan != radius)
                    continue;

                const juce::Point<int> cell { ripple.centre.x + dx, ripple.centre.y + dy };
                if (! bounds.contains (cell))
                    continue;

                recordPerformanceMovementCell (cell);
                triggerPerformanceNotesAtCell (cell);
                performanceFlashes.push_back ({ cell, ripple.colour, 0.82f, true });
            }

        ++ripple.radius;
        if (ripple.radius > ripple.maxRadius)
        {
            ripple.centre = { bounds.getX() + rng.nextInt (bounds.getWidth()),
                              bounds.getY() + rng.nextInt (bounds.getHeight()) };
            ripple.radius = 0;
            ripple.maxRadius = 2 + rng.nextInt (5);
        }
    }
}

void GameComponent::stepPerformanceSequencers()
{
    const auto bounds = getPerformanceRegionBounds();
    if (performanceSequencers.empty())
    {
        resetPerformanceAgents();
        if (performanceSequencers.empty())
            return;
    }

    juce::Random rng;
    auto randomLeftLaunch = [&]() -> std::pair<juce::Point<int>, juce::Point<int>>
    {
        const bool startFromTop = rng.nextBool();
        if (startFromTop)
            return { { bounds.getX(), bounds.getY() }, { 0, 1 } };
        return { { bounds.getX(), bounds.getBottom() - 1 }, { 0, -1 } };
    };

    for (auto& sequencer : performanceSequencers)
    {
        if (! bounds.contains (sequencer.cell))
        {
            sequencer.cell = { bounds.getX(), bounds.getY() };
            sequencer.direction = { 1, 0 };
            sequencer.hasPreviousCell = false;
        }

        recordPerformanceMovementCell (sequencer.cell);
        triggerPerformanceNotesAtCell (sequencer.cell);
        performanceFlashes.push_back ({ sequencer.cell, sequencer.colour, 0.90f, true });

        auto nextCell = sequencer.cell + sequencer.direction;
        if (bounds.contains (nextCell))
        {
            sequencer.previousCell = sequencer.cell;
            sequencer.cell = nextCell;
            sequencer.hasPreviousCell = true;
            continue;
        }

        if (rng.nextBool())
        {
            auto [cornerCell, cornerDirection] = randomLeftLaunch();
            sequencer.previousCell = sequencer.cell;
            sequencer.cell = cornerCell;
            sequencer.direction = cornerDirection;
            sequencer.hasPreviousCell = true;
        }
        else
        {
            if (sequencer.direction.x != 0)
            {
                const int nextRow = juce::jlimit (bounds.getY(), bounds.getBottom() - 1, sequencer.cell.y);
                sequencer.previousCell = sequencer.cell;
                sequencer.cell = { bounds.getX(), nextRow };
                sequencer.direction = { 1, 0 };
            }
            else
            {
                const int nextRow = bounds.getY() + rng.nextInt (bounds.getHeight());
                sequencer.previousCell = sequencer.cell;
                sequencer.cell = { bounds.getX(), nextRow };
                sequencer.direction = { 1, 0 };
            }
            sequencer.hasPreviousCell = true;
        }
    }
}

void GameComponent::stepPerformanceTenori()
{
    const auto bounds = getPerformanceRegionBounds();
    if (bounds.isEmpty())
        return;

    performanceTenoriColumn = juce::jlimit (bounds.getX(), bounds.getRight() - 1, performanceTenoriColumn);

    for (int y = bounds.getY(); y < bounds.getBottom(); ++y)
    {
        const juce::Point<int> cell { performanceTenoriColumn, y };
        recordPerformanceMovementCell (cell);
        triggerPerformanceNotesAtCell (cell);
        performanceFlashes.push_back ({ cell, juce::Colour::fromRGBA (130, 238, 255, 220), 0.64f, true });
    }

    ++performanceTenoriColumn;
    if (performanceTenoriColumn >= bounds.getRight())
        performanceTenoriColumn = bounds.getX();
}

void GameComponent::timerCallback()
{
    for (auto it = performanceFlashes.begin(); it != performanceFlashes.end();)
    {
        it->life -= 0.08f;
        if (it->life <= 0.0f)
            it = performanceFlashes.erase (it);
        else
            ++it;
    }

    if (performanceBeatEnergy > 0.0f)
        performanceBeatEnergy *= 0.94f;

    if (autosavePending)
    {
        if (--autosaveCountdownFrames <= 0)
            performAutosave();
    }

    if (currentScene == Scene::builder && performanceMode)
    {
        performanceSessionSeconds += 1.0 / 30.0;
        if (! performanceSnakes.empty() || ! performanceAutomataCells.empty()
            || ! performanceRipples.empty() || ! performanceSequencers.empty()
            || performanceAgentMode == PerformanceAgentMode::tenori)
        {
            performanceStepAccumulator += performanceBpm / 60.0 / 30.0;
            while (performanceStepAccumulator >= 0.25)
            {
                performanceStepAccumulator -= 0.25;
                ++performanceTick;
                stepPerformanceAgents();
            }
        }

        repaint();
        return;
    }

    if (currentScene == Scene::builder && topDownBuildMode == TopDownBuildMode::tetris)
    {
        ++tetrisGravityTick;
        if (tetrisGravityTick >= tetrisGravityFrames)
        {
            tetrisGravityTick = 0;
            softDropTetrisPiece();
        }
        repaint();
        return;
    }

    if (currentScene == Scene::builder && topDownBuildMode == TopDownBuildMode::none
        && builderViewMode == BuilderViewMode::firstPerson)
    {
        constexpr float playerSpeed = 7.0f / 30.0f;
        constexpr float eyeHeight = 2.35f;
        constexpr float gravityPerTick = 0.055f;
        constexpr float maxFallSpeed = 0.6f;
        constexpr float jumpVelocity = 0.62f;
        constexpr float maxStepUp = 1.05f;
        const float moveX = float ((juce::KeyPress::isKeyCurrentlyDown ('d') ? 1 : 0)
                                 - (juce::KeyPress::isKeyCurrentlyDown ('a') ? 1 : 0));
        const float moveZ = float ((juce::KeyPress::isKeyCurrentlyDown ('w') ? 1 : 0)
                                 - (juce::KeyPress::isKeyCurrentlyDown ('s') ? 1 : 0));
        const bool jumpDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey);

        if (moveX != 0.0f || moveZ != 0.0f)
        {
            const float forwardX = std::sin (firstPersonState.yaw);
            const float forwardY = std::cos (firstPersonState.yaw);
            const float rightX = std::cos (firstPersonState.yaw);
            const float rightY = -std::sin (firstPersonState.yaw);

            float wishX = rightX * moveX + forwardX * moveZ;
            float wishY = rightY * moveX + forwardY * moveZ;
            const float wishLength = std::sqrt (wishX * wishX + wishY * wishY);
            if (wishLength > 0.0f)
            {
                wishX = (wishX / wishLength) * playerSpeed;
                wishY = (wishY / wishLength) * playerSpeed;

                const float nextX = firstPersonState.x + wishX;
                const float nextY = firstPersonState.y + wishY;

                if (isWalkable (nextX, firstPersonState.y, firstPersonState.eyeZ))
                    firstPersonState.x = nextX;
                if (isWalkable (firstPersonState.x, nextY, firstPersonState.eyeZ))
                    firstPersonState.y = nextY;
            }
        }

        const int supportX = juce::jlimit (0, getSurfaceWidth() - 1, static_cast<int> (std::floor (firstPersonState.x)));
        const int supportY = juce::jlimit (0, getSurfaceDepth() - 1, static_cast<int> (std::floor (firstPersonState.y)));
        const float supportedEyeZ = static_cast<float> (getTopSolidZAt (supportX, supportY)) + eyeHeight;
        const bool onGround = std::abs (firstPersonState.eyeZ - supportedEyeZ) <= 0.05f;

        if (jumpDown && ! firstPersonJumpWasDown && onGround)
            firstPersonState.verticalVelocity = jumpVelocity;

        firstPersonJumpWasDown = jumpDown;

        if (firstPersonState.verticalVelocity > 0.0f)
        {
            firstPersonState.verticalVelocity = juce::jmax (-maxFallSpeed, firstPersonState.verticalVelocity - gravityPerTick);
            const float nextEyeZ = firstPersonState.eyeZ + firstPersonState.verticalVelocity;
            if (isWalkable (firstPersonState.x, firstPersonState.y, nextEyeZ))
                firstPersonState.eyeZ = nextEyeZ;
            else
                firstPersonState.verticalVelocity = 0.0f;
        }
        else if (firstPersonState.eyeZ < supportedEyeZ
            && supportedEyeZ - firstPersonState.eyeZ <= maxStepUp
            && isWalkable (firstPersonState.x, firstPersonState.y, supportedEyeZ))
        {
            firstPersonState.eyeZ = supportedEyeZ;
            firstPersonState.verticalVelocity = 0.0f;
        }
        else if (firstPersonState.eyeZ > supportedEyeZ && isWalkable (firstPersonState.x, firstPersonState.y, firstPersonState.eyeZ - 0.05f))
        {
            firstPersonState.verticalVelocity = juce::jlimit (-maxFallSpeed, maxFallSpeed, firstPersonState.verticalVelocity - gravityPerTick);
            const float nextEyeZ = juce::jmax (supportedEyeZ, firstPersonState.eyeZ + firstPersonState.verticalVelocity);
            if (isWalkable (firstPersonState.x, firstPersonState.y, nextEyeZ))
                firstPersonState.eyeZ = nextEyeZ;
            else
                firstPersonState.verticalVelocity = 0.0f;

            if (firstPersonState.eyeZ <= supportedEyeZ + 0.001f)
            {
                firstPersonState.eyeZ = supportedEyeZ;
                firstPersonState.verticalVelocity = 0.0f;
            }
        }
        else
        {
            firstPersonState.eyeZ = supportedEyeZ;
            firstPersonState.verticalVelocity = 0.0f;
        }

        syncCursorToFirstPersonTarget();
    }

    repaint();
}
