#include "MainComponent.h"
#include <algorithm>
#include <cmath>

namespace
{
constexpr float pi = 3.14159265358979323846f;
constexpr std::array<int, 9> arrangementSections { 0, 1, 2, 3, 1, 2, 3, 4, 3 }; // IN,V,PRE,CH,V,PRE,CH,M8,CH
constexpr std::array<int, 5> arrangementBarsPerSection { 2, 4, 2, 4, 2 }; // IN,V,PRE,CH,M8

float wrapPhase(float p)
{
    while (p > juce::MathConstants<float>::twoPi)
        p -= juce::MathConstants<float>::twoPi;
    return p;
}

juce::String toolToString(MainComponent::ToolType t)
{
    switch (t)
    {
        case MainComponent::ToolType::none: return "None";
        case MainComponent::ToolType::redirect: return "Redirect Disc";
        case MainComponent::ToolType::speed: return "Speed Pill";
        case MainComponent::ToolType::ratchet: return "Ratchet Pill";
        case MainComponent::ToolType::key: return "Key Pill";
        case MainComponent::ToolType::scale: return "Scale Pill";
        case MainComponent::ToolType::section: return "Section Tag";
    }
    return "None";
}

juce::String scaleToString(MainComponent::ScaleType s)
{
    switch (s)
    {
        case MainComponent::ScaleType::chromatic: return "Chromatic";
        case MainComponent::ScaleType::major: return "Major";
        case MainComponent::ScaleType::minor: return "Minor";
        case MainComponent::ScaleType::dorian: return "Dorian";
        case MainComponent::ScaleType::pentatonic: return "Pentatonic";
    }
    return "Minor";
}

juce::String playModeToString(MainComponent::PlayMode p)
{
    switch (p)
    {
        case MainComponent::PlayMode::melodic: return "Melodic";
        case MainComponent::PlayMode::chord: return "Chord";
        case MainComponent::PlayMode::arpeggio: return "Arpeggio";
    }
    return "Melodic";
}

juce::String synthToString(MainComponent::SynthEngine s)
{
    switch (s)
    {
        case MainComponent::SynthEngine::digitalV4: return "Nova Drift";
        case MainComponent::SynthEngine::fmGlass: return "Prism FM";
        case MainComponent::SynthEngine::velvetNoise: return "Mallet Bloom";
    }
    return "Nova Drift";
}

MainComponent::ScaleType nextScale(MainComponent::ScaleType s)
{
    const int v = static_cast<int>(s);
    return static_cast<MainComponent::ScaleType>((v + 1) % 5);
}

MainComponent::PlayMode nextPlayMode(MainComponent::PlayMode p)
{
    const int v = static_cast<int>(p);
    return static_cast<MainComponent::PlayMode>((v + 1) % 3);
}

MainComponent::SynthEngine nextSynth(MainComponent::SynthEngine s)
{
    const int v = static_cast<int>(s);
    return static_cast<MainComponent::SynthEngine>((v + 1) % 3);
}

juce::File worldSaveFile()
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile("KlangKunstWorld.mat");
}

int arrangementIndexForSection(int section)
{
    switch (juce::jlimit(0, 4, section))
    {
        case 0: return 0;
        case 1: return 1;
        case 2: return 2;
        case 3: return 3;
        case 4: return 7;
        default: break;
    }
    return 1;
}

int signatureIndexForWorldSize(int size)
{
    if (size <= 12) return 0;
    if (size >= 20) return 2;
    return 1;
}

int beatsPerBarForWorldSize(int size)
{
    if (size <= 12) return 3; // 3/4
    if (size >= 20) return 5; // 5/4
    return 4;                 // 4/4
}
} // namespace

bool MainComponent::WaveVoice::canPlaySound(juce::SynthesiserSound* s)
{
    return dynamic_cast<WaveSound*>(s) != nullptr;
}

void MainComponent::WaveVoice::startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int)
{
    currentSampleRate = getSampleRate();
    level = velocity;
    percussionMode = midiNoteNumber >= 120;
    percussionType = juce::jlimit(0, 3, midiNoteNumber - 120);

    const auto cyclesPerSecond = juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber);
    const auto cyclesPerSample = cyclesPerSecond / currentSampleRate;
    angleDelta = cyclesPerSample * juce::MathConstants<double>::twoPi;
    if (engine == SynthEngine::fmGlass)
        modDelta = cyclesPerSample * 3.141 * juce::MathConstants<double>::twoPi;
    else if (engine == SynthEngine::digitalV4)
        modDelta = cyclesPerSample * 0.613 * juce::MathConstants<double>::twoPi;
    else
        modDelta = cyclesPerSample * 1.997 * juce::MathConstants<double>::twoPi;
    subDelta = cyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;
    if (percussionMode)
    {
        // Decouple drum voices from high MIDI note frequencies to avoid beepy tones.
        double drumHz = 120.0;
        if (percussionType == 0) drumHz = 52.0;      // kick body
        else if (percussionType == 1) drumHz = 182.0; // snare body
        else if (percussionType == 2) drumHz = 420.0; // hat metal tone
        else drumHz = 260.0;                          // accent/noise hit

        const double drumCyclesPerSample = drumHz / currentSampleRate;
        angleDelta = drumCyclesPerSample * juce::MathConstants<double>::twoPi;
        modDelta = drumCyclesPerSample * 2.1 * juce::MathConstants<double>::twoPi;
        subDelta = drumCyclesPerSample * 0.5 * juce::MathConstants<double>::twoPi;
    }
    currentAngle = 0.0;
    modAngle = 0.0;
    subAngle = 0.0;
    noteAgeSeconds = 0.0f;
    noiseSeed = static_cast<uint32_t>(0x9E3779B9u ^ static_cast<uint32_t>(midiNoteNumber * 2654435761u));
    sampleHoldValue = 0.0f;
    sampleHoldCounter = 0;
    sampleHoldPeriod = juce::jlimit(1, 6, 1 + (midiNoteNumber % 6));
    lpState = 0.0f;
    hpState = 0.0f;
    noiseLP = 0.0f;
    noiseHP = 0.0f;
    lastNoise = 0.0f;

    if (percussionMode)
    {
        adsrParams.attack = 0.0005f;
        adsrParams.decay = 0.08f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.03f;
    }
    else if (chordLatchMode)
    {
        // Chord-latch: hold with sustain, then fade over roughly one beat after noteOff.
        const float secondsPerBeat = 60.0f / static_cast<float>(juce::jmax(1.0, bpm));
        adsrParams.attack = 0.003f;
        adsrParams.decay = 0.04f;
        adsrParams.sustain = 1.0f;
        adsrParams.release = juce::jlimit(0.12f, 0.90f, secondsPerBeat);
    }
    else if (engine == SynthEngine::digitalV4)
    {
        // Lyrical pluck/lead with soft bloom.
        adsrParams.attack = 0.006f;
        adsrParams.decay = 0.24f;
        adsrParams.sustain = 0.38f;
        adsrParams.release = 0.30f;
    }
    else if (engine == SynthEngine::velvetNoise)
    {
        // Short mallet/xylophone voice.
        adsrParams.attack = 0.0004f;
        adsrParams.decay = 0.13f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.025f;
    }
    else
    {
        // Soft bell-pluck with shorter but musical tail.
        adsrParams.attack = 0.0018f;
        adsrParams.decay = 0.22f;
        adsrParams.sustain = 0.0f;
        adsrParams.release = 0.20f;
    }
    adsr.setParameters(adsrParams);
    adsr.noteOn();
}

void MainComponent::WaveVoice::stopNote(float, bool allowTailOff)
{
    if (allowTailOff)
        adsr.noteOff();
    else
        clearCurrentNote();
}

void MainComponent::WaveVoice::renderNextBlock(juce::AudioBuffer<float>& outputBuffer,
                                                int startSample,
                                                int numSamples)
{
    if (! isVoiceActive())
        return;

    const auto sr = static_cast<float>(getSampleRate());
    for (int s = 0; s < numSamples; ++s)
    {
        const auto env = adsr.getNextSample();
        const auto transient = std::exp(-noteAgeSeconds * 24.0f);

        // LCG white noise for digital/granular transient energy.
        noiseSeed = noiseSeed * 1664525u + 1013904223u;
        const auto white = static_cast<float>((noiseSeed >> 9) & 0x7FFFFFu) / 4194303.5f * 2.0f - 1.0f;

        if (percussionMode)
        {
            float voicedPerc = 0.0f;
            switch (percussionType)
            {
                case 0:
                {
                    // 909-like kick: pitch-dropped sine body + short click.
                    const float pitchDrop = 1.0f + 4.4f * std::exp(-noteAgeSeconds * 48.0f);
                    const float body = std::sin(currentAngle * pitchDrop);
                    const float bodyEnv = std::exp(-noteAgeSeconds * 11.5f);
                    const float clickEnv = std::exp(-noteAgeSeconds * 165.0f);
                    const float clickTone = std::sin(currentAngle * 10.0);
                    voicedPerc = 0.95f * body * bodyEnv + 0.19f * clickTone * clickEnv + 0.08f * white * clickEnv;
                    break;
                }
                case 1:
                {
                    // 909-ish snare: tonal body + filtered noise snap.
                    noiseLP += 0.15f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float snapEnv = std::exp(-noteAgeSeconds * 47.0f);
                    const float toneEnv = std::exp(-noteAgeSeconds * 20.0f);
                    const float tone1 = std::sin(currentAngle * 1.86);
                    const float tone2 = std::sin(currentAngle * 2.71);
                    const float preSnap = (white - lastNoise);
                    voicedPerc = 0.42f * tone1 * toneEnv
                               + 0.18f * tone2 * toneEnv
                               + (0.62f * noiseHP + 0.18f * preSnap) * snapEnv;
                    break;
                }
                case 2:
                {
                    // Closed hat: short metallic burst, less harsh.
                    noiseLP += 0.24f * (white - noiseLP);
                    noiseHP = white - noiseLP;
                    const float hat = std::exp(-noteAgeSeconds * 92.0f);
                    const auto metallic = std::copysign(1.0f, white * 0.78f + noiseHP * 0.22f);
                    voicedPerc = (0.56f * metallic + 0.30f * noiseHP) * hat;
                    break;
                }
                case 3:
                default:
                {
                    const auto diff = white - lastNoise;
                    lastNoise = white;
                    const auto clickEnv = static_cast<float>(std::exp(-noteAgeSeconds * 96.0f));
                    const auto gate = (sampleHoldCounter <= 0 ? 1.0f : 0.0f);
                    if (sampleHoldCounter <= 0)
                        sampleHoldCounter = 2 + static_cast<int>(noiseSeed & 3u);
                    --sampleHoldCounter;
                    voicedPerc = (0.74f * diff + 0.16f * white) * clickEnv * gate * 1.6f;
                    break;
                }
            }

            voicedPerc = std::tanh(voicedPerc * 1.75f) * 0.69f;
            const float sample = voicedPerc * level * env * 0.80f;
            for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
                outputBuffer.addSample(c, startSample + s, sample);

            currentAngle += angleDelta;
            modAngle += modDelta;
            subAngle += subDelta;
            if (currentAngle >= juce::MathConstants<double>::twoPi) currentAngle -= juce::MathConstants<double>::twoPi;
            if (modAngle >= juce::MathConstants<double>::twoPi) modAngle -= juce::MathConstants<double>::twoPi;
            if (subAngle >= juce::MathConstants<double>::twoPi) subAngle -= juce::MathConstants<double>::twoPi;
            noteAgeSeconds += 1.0f / sr;
            continue;
        }

        const auto sub = static_cast<float>(std::sin(subAngle));
        const auto click = white * transient * 0.18f;

        float voiced = 0.0f;
        if (engine == SynthEngine::digitalV4)
        {
            // Nova Drift: overtone-rich but consonant, with choir-like movement.
            const auto vibrato = 0.009 * std::sin(modAngle * 0.14);
            const auto oscA = std::sin(currentAngle + vibrato);
            const auto oscB = std::sin(currentAngle * 2.0 + 0.12 * std::sin(modAngle * 0.10));
            const auto oscC = std::sin(currentAngle * 3.0 + 0.06 * std::sin(modAngle * 0.06));
            const auto subWarm = std::sin(subAngle + 0.05 * std::sin(modAngle * 0.05));
            const auto raw = static_cast<float>(0.67 * oscA + 0.14 * oscB + 0.06 * oscC + 0.16 * subWarm + click * 0.002f);

            const auto cutoff = 180.0f + 1020.0f * env + 130.0f * static_cast<float>(0.5 + 0.5 * std::sin(modAngle * 0.03));
            const auto alpha = std::exp(-juce::MathConstants<float>::twoPi * cutoff / sr);
            lpState = alpha * lpState + (1.0f - alpha) * raw;
            const auto hp = raw - lpState;
            hpState = 0.996f * hpState + 0.004f * hp;

            voiced = std::tanh((0.97f * lpState + 0.005f * hpState + 0.06f * subWarm) * 0.90f) * 0.66f;
        }
        else if (engine == SynthEngine::fmGlass)
        {
            // Prism FM: harmonic integer-ratio FM with a glassy but sweet top.
            const float idx = 1.35f * std::exp(-noteAgeSeconds * 7.4f) + 0.08f;
            const auto carrier = std::sin(currentAngle + idx * std::sin(modAngle));
            const auto side2 = std::sin(currentAngle * 2.0 + idx * 0.30f * std::sin(modAngle * 0.5));
            const auto side3 = std::sin(currentAngle * 3.0 + idx * 0.16f * std::sin(modAngle * 0.31));
            const auto air = std::sin(currentAngle * 4.0 + modAngle * 0.09);
            const auto raw = static_cast<float>(0.75 * carrier + 0.16 * side2 + 0.06 * side3 + 0.03 * air + click * 0.02f);

            const float hp = raw - lpState;
            lpState += 0.11f * (raw - lpState);
            hpState += 0.02f * (hp - hpState);
            voiced = std::tanh((0.94f * raw + 0.05f * hpState) * 0.92f) * 0.66f;
        }
        else
        {
            // Mallet Bloom: short woody strike + tuned resonators (xylophone-like).
            const float toneEnv = std::exp(-noteAgeSeconds * 9.5f);
            const float metalEnv = std::exp(-noteAgeSeconds * 17.0f);
            const float strikeEnv = std::exp(-noteAgeSeconds * 95.0f);

            noiseLP += 0.36f * (white - noiseLP);
            const float strike = (0.72f * white + 0.28f * (white - noiseLP)) * strikeEnv * 0.20f;

            const auto fundamental = std::sin(currentAngle);
            const auto r2 = std::sin(currentAngle * 3.99 + 0.12 * std::sin(modAngle * 0.3));
            const auto r3 = std::sin(currentAngle * 6.83 + 0.08 * std::sin(modAngle * 0.43));
            const auto r4 = std::sin(currentAngle * 9.77 + 0.05 * std::sin(modAngle * 0.57));

            const auto body = static_cast<float>(0.74 * fundamental * toneEnv
                                               + 0.17 * r2 * metalEnv
                                               + 0.07 * r3 * metalEnv
                                               + 0.04 * r4 * metalEnv
                                               + 0.06 * sub * toneEnv);

            lpState += 0.18f * ((body + strike) - lpState);
            voiced = std::tanh((0.82f * lpState + 0.18f * body + strike) * 1.34f) * 0.72f;
        }

        const float sample = voiced * level * env;

        for (int c = 0; c < outputBuffer.getNumChannels(); ++c)
            outputBuffer.addSample(c, startSample + s, sample);

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

void MainComponent::Miverb::prepare(double sampleRate)
{
    sr = juce::jmax(16000.0, sampleRate);
    const float k = static_cast<float>(sr / 32000.0);

    ap1.resize(static_cast<int>(113.0f * k) + 64);
    ap2.resize(static_cast<int>(162.0f * k) + 64);
    ap3.resize(static_cast<int>(241.0f * k) + 64);
    ap4.resize(static_cast<int>(399.0f * k) + 64);
    tankLAp.resize(static_cast<int>(1253.0f * k) + 64);
    tankRAp.resize(static_cast<int>(1513.0f * k) + 64);
    tankLDelay.resize(static_cast<int>(3411.0f * k) + 256);
    tankRDelay.resize(static_cast<int>(4782.0f * k) + 256);
    reset();
}

void MainComponent::Miverb::reset()
{
    ap1.clear();
    ap2.clear();
    ap3.clear();
    ap4.clear();
    tankLAp.clear();
    tankRAp.clear();
    tankLDelay.clear();
    tankRDelay.clear();
    lpL = lpR = hpL = hpR = 0.0f;
    phase = 0.0f;
}

float MainComponent::Miverb::readLinear(const DelayLine& d, float delaySamples) const
{
    if (d.data.empty())
        return 0.0f;

    const int n = static_cast<int>(d.data.size());
    float read = static_cast<float>(d.write) - delaySamples;
    while (read < 0.0f)
        read += static_cast<float>(n);
    while (read >= static_cast<float>(n))
        read -= static_cast<float>(n);

    const int i0 = static_cast<int>(read);
    const int i1 = (i0 + 1) % n;
    const float frac = read - static_cast<float>(i0);
    return d.data[static_cast<size_t>(i0)] + frac * (d.data[static_cast<size_t>(i1)] - d.data[static_cast<size_t>(i0)]);
}

float MainComponent::Miverb::allpassProcess(DelayLine& d, float input, float delaySamples, float g)
{
    const float delayed = readLinear(d, delaySamples);
    const float y = -g * input + delayed;
    const float writeSample = input + g * y;
    d.data[static_cast<size_t>(d.write)] = writeSample;
    d.write = (d.write + 1) % static_cast<int>(d.data.size());
    return y;
}

float MainComponent::Miverb::delayProcess(DelayLine& d, float input, float delaySamples)
{
    const float delayed = readLinear(d, delaySamples);
    d.data[static_cast<size_t>(d.write)] = input;
    d.write = (d.write + 1) % static_cast<int>(d.data.size());
    return delayed;
}

void MainComponent::Miverb::process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (buffer.getNumChannels() < 2 || numSamples <= 0 || mix <= 0.001f)
        return;

    auto* left = buffer.getWritePointer(0, startSample);
    auto* right = buffer.getWritePointer(1, startSample);

    const float sizeScale = 0.18f + 0.82f * size;
    const float ap1d = 28.0f + 82.0f * sizeScale;
    const float ap2d = 38.0f + 118.0f * sizeScale;
    const float ap3d = 55.0f + 170.0f * sizeScale;
    const float ap4d = 88.0f + 280.0f * sizeScale;
    const float tankApLd = 340.0f + 760.0f * sizeScale;
    const float tankApRd = 420.0f + 900.0f * sizeScale;
    const float tankLd = 950.0f + 2800.0f * sizeScale;
    const float tankRd = 1300.0f + 3500.0f * sizeScale;
    const float g = juce::jlimit(0.25f, 0.95f, diffusion);

    for (int i = 0; i < numSamples; ++i)
    {
        const float inL = left[i];
        const float inR = right[i];
        const float mono = (inL + inR) * 0.5f;

        const float lfo1 = std::sin(phase);
        const float lfo2 = std::sin(phase * 1.21f + 1.7f);
        phase += juce::MathConstants<float>::twoPi * (modRate / static_cast<float>(sr));
        if (phase > juce::MathConstants<float>::twoPi)
            phase -= juce::MathConstants<float>::twoPi;

        const float apOut1 = allpassProcess(ap1, mono, ap1d + modAmount * lfo1, g);
        const float apOut2 = allpassProcess(ap2, apOut1, ap2d + modAmount * 0.6f * lfo2, g);
        const float apOut3 = allpassProcess(ap3, apOut2, ap3d, g);
        const float apOut4 = allpassProcess(ap4, apOut3, ap4d, g);

        const float oldL = readLinear(tankLDelay, tankLd + modAmount * 0.35f * lfo2);
        const float oldR = readLinear(tankRDelay, tankRd + modAmount * 0.35f * lfo1);

        lpL += (lowpass * 0.22f) * (oldL - lpL);
        lpR += (lowpass * 0.22f) * (oldR - lpR);
        hpL += (highpass * 0.12f) * (lpL - hpL);
        hpR += (highpass * 0.12f) * (lpR - hpR);
        const float fbL = lpL - hpL;
        const float fbR = lpR - hpR;

        const float tankInL = apOut4 + fbR * decay;
        const float tankInR = apOut4 + fbL * decay;

        const float tapL = allpassProcess(tankLAp, tankInL, tankApLd + modAmount * 0.4f * lfo1, g * 0.72f);
        const float tapR = allpassProcess(tankRAp, tankInR, tankApRd + modAmount * 0.4f * lfo2, g * 0.72f);

        const float wetL = delayProcess(tankLDelay, tapL, tankLd + modAmount * lfo1);
        const float wetR = delayProcess(tankRDelay, tapR, tankRd + modAmount * lfo2);

        left[i] = inL + (wetL - inL) * mix;
        right[i] = inR + (wetR - inR) * mix;
    }
}

MainComponent::MainComponent()
{
    setOpaque(true);
    setWantsKeyboardFocus(true);
    addKeyListener(this);

    for (int i = 0; i < 12; ++i)
        synth.addVoice(new WaveVoice(synthEngine, bpm, chordLatchMode));
    synth.addSound(new WaveSound());
    for (int i = 0; i < 6; ++i)
        beatSynth.addVoice(new WaveVoice(synthEngine, bpm, chordLatchMode));
    beatSynth.addSound(new WaveSound());

    snakeA.colour = juce::Colour(0xff00ffaa);
    snakeB.colour = juce::Colour(0xffffb347);
    snakeC.colour = juce::Colour(0xff9d7bff);
    snakeD.colour = juce::Colour(0xffff5ed8);
    snakeA.cell = { 0, 0 };
    snakeB.cell = { worldSize - 1, worldSize - 1 };
    snakeC.cell = { worldSize - 1, 0 };
    snakeD.cell = { 0, worldSize - 1 };
    snakeA.dir = { 1, 0 };
    snakeB.dir = { -1, 0 };
    snakeC.dir = { 0, 1 };
    snakeD.dir = { 0, -1 };

    resetBlankWorld();

    setSize(1440, 900);
    setAudioChannels(0, 2);
    lastTimerMs = juce::Time::getMillisecondCounterHiRes();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    shutdownAudio();
}

void MainComponent::prepareToPlay(int, double sampleRate)
{
    const juce::ScopedLock sl(synthLock);
    currentSr = sampleRate;
    synth.setCurrentPlaybackSampleRate(sampleRate);
    beatSynth.setCurrentPlaybackSampleRate(sampleRate);
    miverb.prepare(sampleRate);
    miverb.setMix(miverbMix);
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill)
{
    bufferToFill.clearActiveBufferRegion();

    const juce::ScopedLock sl(synthLock);
    synth.renderNextBlock(*bufferToFill.buffer, juce::MidiBuffer(), bufferToFill.startSample, bufferToFill.numSamples);
    if (miverbEnabled && miverbMix > 0.001f)
    {
        miverb.setMix(miverbMix);
        miverb.setSize(0.64f + 0.12f * std::sin(titlePhase * 0.17f));
        miverb.setDecay(0.84f + 0.10f * globalJuice);
        miverb.setDiffusion(0.72f + 0.14f * beatFlash);
        miverb.setTone(0.66f + 0.18f * beatFlash, 0.06f);
        miverb.process(*bufferToFill.buffer, bufferToFill.startSample, bufferToFill.numSamples);
    }

    // Beat layer is rendered post-reverb so it always stays dry/punchy.
    beatSynth.renderNextBlock(*bufferToFill.buffer, juce::MidiBuffer(), bufferToFill.startSample, bufferToFill.numSamples);
}

void MainComponent::releaseResources()
{
}

void MainComponent::resetBlankWorld()
{
    for (auto& col : columns)
        for (auto& cell : col)
            cell = 0;

    for (auto& col : tools)
        for (auto& t : col)
            t = {};

    p1 = { 2, 2, 0, juce::Colours::cyan };
    p2 = { worldSize - 3, worldSize - 3, 0, juce::Colours::orange };
    snakeA.cell = { 0, 0 };
    snakeB.cell = { worldSize - 1, worldSize - 1 };
    snakeC.cell = { worldSize - 1, 0 };
    snakeD.cell = { 0, worldSize - 1 };
    snakeA.dir = { 1, 0 };
    snakeB.dir = { -1, 0 };
    snakeC.dir = { 0, 1 };
    snakeD.dir = { 0, -1 };
    snakeA.speedState = snakeB.speedState = 1;
    snakeA.ratchet = snakeB.ratchet = 1;
    snakeC.speedState = snakeD.speedState = 1;
    snakeC.ratchet = snakeD.ratchet = 1;
    snakeA.trail.clear();
    snakeB.trail.clear();
    snakeC.trail.clear();
    snakeD.trail.clear();
    snakeC.trail.clear();
    snakeD.trail.clear();
    modeASnakeCount = 1;
    p1Points = 0;
    p2Points = 0;
    randomRedirectCells.clear();
    for (auto& col : randomRedirectActive)
        for (auto& v : col)
            v = false;
    for (auto& col : randomRedirectRotation)
        for (auto& v : col)
            v = 0;
    pulses.clear();
    sparks.clear();
    globalJuice = 0.0f;
    beatFlash = 0.0f;
    screenShake = 0.0f;
    beatStyle = 0;
    miverbEnabled = false;
    miverbMix = 0.00f;
    nextBeatLayerBeat = 0.0;
    arrangementEnabled = true;
    arrangementSequenceIndex = 0;
    arrangementBarsInSection = 0;

    invalidateAllCaches();
}

void MainComponent::loadDemoWorld()
{
    resetBlankWorld();

    for (int x = 0; x < worldSize; ++x)
    {
        const int h1 = 1 + (x % 7);
        const int h2 = 5 + (x % 5);
        for (int z = 1; z <= h1; ++z)
            setBlock(x, x, z, true);
        for (int z = 1; z <= h2; ++z)
            setBlock(x, worldSize - 1 - x, z, true);
    }

    for (int x = 2; x < worldSize - 2; ++x)
        for (int z = 1; z <= 3 + ((x * 2) % 9); ++z)
            setBlock(x, worldSize / 2, z, true);

    tools[3][3] = { ToolType::speed, 0, 2 };
    tools[6][7] = { ToolType::redirect, 1, 0 };
    tools[10][11] = { ToolType::ratchet, 0, 1 };
    tools[12][5] = { ToolType::section, 0, 1 };

    hasSession = true;
    invalidateAllCaches();
}

void MainComponent::invalidateAllCaches()
{
    titleCacheDirty = true;
    buildCacheDirty = true;
    performanceCacheDirty = true;
}

void MainComponent::resized()
{
    invalidateAllCaches();
}

void MainComponent::paint(juce::Graphics& g)
{
    if (mode == Mode::title)
        paintTitle(g);
    else if (mode == Mode::build)
        paintBuild(g);
    else
        paintPerformance(g);
}

void MainComponent::updateTitleCache()
{
    if (! titleCacheDirty && titleCache.isValid() &&
        titleCache.getWidth() == getWidth() && titleCache.getHeight() == getHeight())
        return;

    titleCache = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(titleCache);

    const auto area = getLocalBounds().toFloat();
    const auto c = area.getCentre();

    juce::ColourGradient bg(juce::Colour::fromHSV(std::fmod(titlePhase * 0.1f, 1.0f), 0.7f, 0.08f, 1.0f),
                            0, 0,
                            juce::Colour::fromHSV(std::fmod(titlePhase * 0.1f + 0.3f, 1.0f), 0.85f, 0.18f, 1.0f),
                            area.getWidth(), area.getHeight(),
                            false);
    bg.addColour(0.5, juce::Colour::fromHSV(std::fmod(titlePhase * 0.1f + 0.6f, 1.0f), 0.7f, 0.12f, 1.0f));
    g.setGradientFill(bg);
    g.fillAll();

    for (int ring = 0; ring < 8; ++ring)
    {
        const float r = 80.0f + ring * 56.0f;
        juce::Path poly;
        const int sides = 6 + (ring % 5);
        for (int i = 0; i <= sides; ++i)
        {
            const float a = (juce::MathConstants<float>::twoPi * i / sides) + titlePhase * (0.06f + ring * 0.01f);
            const juce::Point<float> p(c.x + std::cos(a) * r, c.y + std::sin(a) * r);
            if (i == 0)
                poly.startNewSubPath(p);
            else
                poly.lineTo(p);
        }
        const auto col = juce::Colour::fromHSV(std::fmod(0.12f * ring + titlePhase * 0.05f, 1.0f), 0.8f, 0.75f, 0.42f);
        g.setColour(col);
        g.strokePath(poly, juce::PathStrokeType(2.2f));
    }

    for (int i = 0; i < 64; ++i)
    {
        const float a = (juce::MathConstants<float>::twoPi * i / 64.0f) + titlePhase * 0.22f;
        const float r0 = 30.0f + (i % 3) * 14.0f;
        const float r1 = area.getWidth() * 0.52f;
        g.setColour(juce::Colour::fromHSV(std::fmod(0.015f * i + titlePhase * 0.08f, 1.0f), 0.7f, 0.8f, 0.23f));
        g.drawLine(c.x + std::cos(a) * r0,
                   c.y + std::sin(a) * r0,
                   c.x + std::cos(a) * r1,
                   c.y + std::sin(a) * r1,
                   1.0f);
    }

    titleCacheDirty = false;
}

void MainComponent::drawTitleLiveOverlays(juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    const auto centre = area.getCentre();
    const float wobble = std::sin(titlePhase * 1.7f) * (3.0f + globalJuice * 14.0f);

    g.setColour(juce::Colour::fromHSV(std::fmod(titlePhase * 0.12f, 1.0f), 0.7f, 0.8f, 0.09f + globalJuice * 0.18f));
    g.fillEllipse(centre.x - 180.0f - wobble, centre.y - 180.0f - wobble, 360.0f + wobble * 2.0f, 360.0f + wobble * 2.0f);

    for (int y = 0; y < getHeight(); y += 3)
    {
        g.setColour(juce::Colour(0x10ffffff));
        g.drawHorizontalLine(y, 0.0f, static_cast<float>(getWidth()));
    }

    for (int i = 0; i < 4; ++i)
    {
        g.setColour(juce::Colour::fromHSV(std::fmod(titlePhase * 0.15f + 0.2f * i, 1.0f), 0.9f, 0.95f, 0.11f));
        g.drawEllipse(centre.x - 95.0f - i * 9.0f,
                      centre.y - 95.0f - i * 9.0f,
                      190.0f + i * 18.0f,
                      190.0f + i * 18.0f,
                      2.0f);
    }

    for (const auto& s : sparks)
    {
        const float t = s.age / s.maxAge;
        g.setColour(s.c.withAlpha((1.0f - t) * 0.9f));
        g.fillEllipse(s.p.x - s.size, s.p.y - s.size, s.size * 2.0f, s.size * 2.0f);
    }

    g.setFont(juce::Font("Avenir Next", 96.0f, juce::Font::bold));
    g.setColour(juce::Colour(0xffff2c95).withAlpha(0.38f + globalJuice * 0.3f));
    g.drawText("KlangKunst", getLocalBounds().translated(static_cast<int>(-3.0f - wobble * 0.3f), static_cast<int>(2.0f + wobble * 0.1f)), juce::Justification::centredTop, true);
    g.setColour(juce::Colour(0xff16e9ff).withAlpha(0.38f + globalJuice * 0.3f));
    g.drawText("KlangKunst", getLocalBounds().translated(static_cast<int>(3.0f + wobble * 0.3f), static_cast<int>(-2.0f - wobble * 0.1f)), juce::Justification::centredTop, true);
    g.setColour(juce::Colours::white.withAlpha(0.08f));
    g.drawText("KlangKunst", getLocalBounds().translated(4, 4), juce::Justification::centredTop, true);
    g.setColour(juce::Colours::white.withAlpha(0.95f));
    g.drawText("KlangKunst", getLocalBounds().translated(0, 0), juce::Justification::centredTop, true);

    g.setFont(juce::Font("Avenir Next", 26.0f, juce::Font::plain));
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("Building Music / Playing Architecture", 0, 110, getWidth(), 40, juce::Justification::centred);

    g.setFont(juce::Font("Avenir Next", 22.0f, juce::Font::bold));
    g.drawText("matd.space", 24, 16, 240, 30, juce::Justification::topLeft);

    const auto menu = getTitleMenu();
    const int panelHeight = juce::jmax(300, 130 + static_cast<int>(menu.size()) * 56);
    juce::Rectangle<int> panel(getWidth() / 2 - 230, getHeight() / 2 - panelHeight / 2, 460, panelHeight);
    g.setColour(juce::Colours::black.withAlpha(0.55f));
    g.fillRoundedRectangle(panel.toFloat(), 8.0f);
    g.setColour(juce::Colour(0xff00f6ff).withAlpha(0.12f + globalJuice * 0.28f));
    g.drawRoundedRectangle(panel.toFloat().expanded(6.0f), 12.0f, 5.0f);
    g.setColour(juce::Colours::white.withAlpha(0.85f));
    g.drawRoundedRectangle(panel.toFloat(), 8.0f, 2.0f);

    g.setFont(juce::Font("Avenir Next", 28.0f, juce::Font::bold));
    for (size_t i = 0; i < menu.size(); ++i)
    {
        juce::String text;
        const bool enabled = isTitleActionEnabled(menu[i]);
        switch (menu[i])
        {
            case MenuAction::resume: text = "Resume"; break;
            case MenuAction::save: text = "Save File"; break;
            case MenuAction::load: text = "Load Saved File"; break;
            case MenuAction::demo: text = "Start Demo Song"; break;
            case MenuAction::blank: text = "Start Blank World"; break;
            case MenuAction::quit: text = "Quit"; break;
        }

        juce::Rectangle<int> row(panel.getX() + 20, panel.getY() + 65 + static_cast<int>(i) * 56, panel.getWidth() - 40, 42);
        if (static_cast<int>(i) == selectedMenu && enabled)
        {
            g.setColour(juce::Colour::fromRGBA(20, 220, 255, 80));
            g.fillRoundedRectangle(row.toFloat(), 6.0f);
            g.setColour(juce::Colours::cyan);
            g.drawRoundedRectangle(row.toFloat(), 6.0f, 1.5f);
        }
        g.setColour(juce::Colours::white.withAlpha(enabled ? 0.95f : 0.35f));
        g.drawText(text, row, juce::Justification::centred);
    }

    g.setFont(juce::Font("Avenir Next", 16.0f, juce::Font::plain));
    g.setColour(juce::Colours::white.withAlpha(0.7f));
    g.drawText("Use Up/Down + Enter", panel.withY(panel.getBottom() - 34), juce::Justification::centred);

    if (selectingBlankWorldSize)
    {
        auto chooser = area.withSizeKeepingCentre(560.0f, 290.0f);
        g.setColour(juce::Colour(0xee070b14));
        g.fillRoundedRectangle(chooser, 14.0f);
        g.setColour(juce::Colour(0xff2df8ff).withAlpha(0.9f));
        g.drawRoundedRectangle(chooser, 14.0f, 2.0f);

        g.setFont(juce::Font("Avenir Next", 26.0f, juce::Font::bold));
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.drawText("Choose Time Signature / World Size", chooser.removeFromTop(58.0f).toNearestInt(), juce::Justification::centred);

        const juce::String labels[3] = { "3/4  ->  12x12", "4/4  ->  16x16", "5/4  ->  20x20" };
        for (int i = 0; i < 3; ++i)
        {
            auto row = chooser.removeFromTop(58.0f).reduced(34.0f, 6.0f);
            const bool sel = (i == blankWorldSignatureIndex);
            const juce::Colour c = sel ? juce::Colour(0xff16e9ff) : juce::Colour(0x77ffffff);
            g.setColour(c.withAlpha(sel ? 0.20f : 0.10f));
            g.fillRoundedRectangle(row, 10.0f);
            g.setColour(c.withAlpha(sel ? 0.95f : 0.70f));
            g.drawRoundedRectangle(row, 10.0f, sel ? 2.2f : 1.2f);
            g.setFont(juce::Font("Avenir Next", sel ? 24.0f : 22.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawText(labels[i], row.toNearestInt(), juce::Justification::centred);
        }

        g.setFont(juce::Font("Avenir Next", 16.0f, juce::Font::plain));
        g.setColour(juce::Colours::white.withAlpha(0.74f));
        g.drawText("Up/Down to choose, Enter to confirm, Esc to cancel",
                   chooser.withTrimmedTop(8.0f).toNearestInt(),
                   juce::Justification::centred);
    }
}

void MainComponent::paintTitle(juce::Graphics& g)
{
    updateTitleCache();
    const float shakeAmount = screenShake * 20.0f;
    const float dx = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    const float dy = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    g.addTransform(juce::AffineTransform::translation(dx, dy));
    g.drawImageAt(titleCache, 0, 0);
    drawTitleLiveOverlays(g);
}

juce::Point<int> MainComponent::rotateCell(int x, int y, int rot) const
{
    switch ((rot % 4 + 4) % 4)
    {
        case 0: return { x, y };
        case 1: return { worldSize - 1 - y, x };
        case 2: return { worldSize - 1 - x, worldSize - 1 - y };
        case 3: return { y, worldSize - 1 - x };
    }
    return { x, y };
}

juce::Point<float> MainComponent::isoToScreen(int x, int y, int z, float tileW, float tileH, juce::Point<float> origin, int rot) const
{
    const auto r = rotateCell(x, y, rot);
    const float sx = origin.x + (r.x - r.y) * tileW * 0.5f;
    const float sy = origin.y + (r.x + r.y) * tileH * 0.5f - z * tileH;
    return { sx, sy };
}

juce::Point<float> MainComponent::getBuildOrigin(float, float tileH) const
{
    const float floorMidYOffset = (static_cast<float>(worldSize - 1) * tileH * 0.5f);
    const float extraLift = (worldSize >= 20 ? 25.0f : 0.0f);
    return { getWidth() * 0.5f, getHeight() * 0.5f - floorMidYOffset + 125.0f - extraLift };
}

juce::Colour MainComponent::pitchClassColour(int semitone) const
{
    const float h = std::fmod((static_cast<float>(semitone % 12) / 12.0f) + 1.0f, 1.0f);
    return juce::Colour::fromHSV(h, 0.7f, 0.95f, 1.0f);
}

int MainComponent::zToMidi(int z) const
{
    if (z <= 0)
        return -1;
    return 60 + (z - 1);
}

bool MainComponent::hasBlock(int x, int y, int z) const
{
    if (x < 0 || y < 0 || x >= worldSize || y >= worldSize || z <= 0 || z >= maxHeight)
        return false;
    const uint32_t mask = (1u << static_cast<uint32_t>(z));
    return (columns[static_cast<size_t>(x)][static_cast<size_t>(y)] & mask) != 0u;
}

void MainComponent::setBlock(int x, int y, int z, bool enabled)
{
    if (x < 0 || y < 0 || x >= worldSize || y >= worldSize || z <= 0 || z >= maxHeight)
        return;

    uint32_t& cell = columns[static_cast<size_t>(x)][static_cast<size_t>(y)];
    const uint32_t mask = (1u << static_cast<uint32_t>(z));
    if (enabled)
        cell |= mask;
    else
        cell &= ~mask;
}

int MainComponent::topBlockZ(int x, int y) const
{
    if (x < 0 || y < 0 || x >= worldSize || y >= worldSize)
        return 0;

    const uint32_t cell = columns[static_cast<size_t>(x)][static_cast<size_t>(y)];
    for (int z = maxHeight - 1; z >= 1; --z)
        if ((cell & (1u << static_cast<uint32_t>(z))) != 0u)
            return z;
    return 0;
}

std::vector<int> MainComponent::currentScaleSteps() const
{
    switch (scale)
    {
        case ScaleType::chromatic: return { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11 };
        case ScaleType::major: return { 0, 2, 4, 5, 7, 9, 11 };
        case ScaleType::minor: return { 0, 2, 3, 5, 7, 8, 10 };
        case ScaleType::dorian: return { 0, 2, 3, 5, 7, 9, 10 };
        case ScaleType::pentatonic: return { 0, 2, 4, 7, 9 };
    }
    return { 0, 2, 3, 5, 7, 8, 10 };
}

int MainComponent::quantizeMidiToScale(int midi) const
{
    if (! quantizeToScale)
        return midi;

    const auto steps = currentScaleSteps();
    int bestNote = midi;
    int bestDistance = 128;

    for (int n = midi - 12; n <= midi + 12; ++n)
    {
        const int pc = (n % 12 + 12) % 12;
        const int rel = (pc - keyRoot + 12) % 12;
        if (std::find(steps.begin(), steps.end(), rel) != steps.end())
        {
            const int d = std::abs(n - midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                bestNote = n;
            }
        }
    }
    return bestNote;
}

int MainComponent::quantizeMidiToCurrentScaleStrict(int midi) const
{
    const auto steps = currentScaleSteps();
    int bestNote = midi;
    int bestDistance = 128;

    for (int n = midi - 12; n <= midi + 12; ++n)
    {
        const int pc = (n % 12 + 12) % 12;
        const int rel = (pc - keyRoot + 12) % 12;
        if (std::find(steps.begin(), steps.end(), rel) != steps.end())
        {
            const int d = std::abs(n - midi);
            if (d < bestDistance)
            {
                bestDistance = d;
                bestNote = n;
            }
        }
    }
    return bestNote;
}

bool MainComponent::quantizeWorldToCurrentScale()
{
    bool changed = false;

    for (int y = 0; y < worldSize; ++y)
    {
        for (int x = 0; x < worldSize; ++x)
        {
            uint32_t oldCol = columns[static_cast<size_t>(x)][static_cast<size_t>(y)];
            uint32_t newCol = 0u;

            for (int z = 1; z < maxHeight; ++z)
            {
                if ((oldCol & (1u << static_cast<uint32_t>(z))) == 0u)
                    continue;

                const int qMidi = quantizeMidiToCurrentScaleStrict(zToMidi(z));
                const int qZ = juce::jlimit(1, maxHeight - 1, (qMidi - 60) + 1);
                newCol |= (1u << static_cast<uint32_t>(qZ));
            }

            if (newCol != oldCol)
            {
                columns[static_cast<size_t>(x)][static_cast<size_t>(y)] = newCol;
                changed = true;
            }
        }
    }

    return changed;
}

void MainComponent::updateBuildCache()
{
    if (! buildCacheDirty && buildCache.isValid() &&
        buildCache.getWidth() == getWidth() && buildCache.getHeight() == getHeight())
        return;

    buildCache = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(buildCache);
    g.fillAll(juce::Colour(0xff0c0f15));

    const float tileW = 64.0f;
    const float tileH = 32.0f;
    const juce::Point<float> origin = getBuildOrigin(tileW, tileH);

    for (int gx = 0; gx <= worldSize; ++gx)
    {
        const auto a = isoToScreen(gx, 0, 0, tileW, tileH, origin, viewRotation);
        const auto b = isoToScreen(gx, worldSize, 0, tileW, tileH, origin, viewRotation);
        g.setColour(juce::Colour(0xff273143));
        g.drawLine(a.x, a.y, b.x, b.y, 1.0f);
    }

    for (int gy = 0; gy <= worldSize; ++gy)
    {
        const auto a = isoToScreen(0, gy, 0, tileW, tileH, origin, viewRotation);
        const auto b = isoToScreen(worldSize, gy, 0, tileW, tileH, origin, viewRotation);
        g.setColour(juce::Colour(0xff273143));
        g.drawLine(a.x, a.y, b.x, b.y, 1.0f);
    }

    for (int y = 0; y < worldSize; ++y)
    {
        for (int x = 0; x < worldSize; ++x)
        {
            for (int z = 1; z < maxHeight; ++z)
            {
                if (! hasBlock(x, y, z))
                    continue;

                const juce::Colour c = pitchClassColour((z - 1) % 12).withSaturation(0.64f);

                const auto t00 = isoToScreen(x,     y,     z, tileW, tileH, origin, viewRotation);
                const auto t10 = isoToScreen(x + 1, y,     z, tileW, tileH, origin, viewRotation);
                const auto t11 = isoToScreen(x + 1, y + 1, z, tileW, tileH, origin, viewRotation);
                const auto t01 = isoToScreen(x,     y + 1, z, tileW, tileH, origin, viewRotation);
                const auto top = (t00 + t10 + t11 + t01) * 0.25f;

                juce::Path topFace;
                topFace.startNewSubPath(top.x, top.y - tileH * 0.5f);
                topFace.lineTo(top.x + tileW * 0.5f, top.y);
                topFace.lineTo(top.x, top.y + tileH * 0.5f);
                topFace.lineTo(top.x - tileW * 0.5f, top.y);
                topFace.closeSubPath();
                g.setColour(c.brighter(0.22f));
                g.fillPath(topFace);

                juce::Path leftFace;
                leftFace.startNewSubPath(top.x - tileW * 0.5f, top.y);
                leftFace.lineTo(top.x, top.y + tileH * 0.5f);
                leftFace.lineTo(top.x, top.y + tileH * 1.5f);
                leftFace.lineTo(top.x - tileW * 0.5f, top.y + tileH);
                leftFace.closeSubPath();
                g.setColour(c.darker(0.35f));
                g.fillPath(leftFace);

                juce::Path rightFace;
                rightFace.startNewSubPath(top.x + tileW * 0.5f, top.y);
                rightFace.lineTo(top.x, top.y + tileH * 0.5f);
                rightFace.lineTo(top.x, top.y + tileH * 1.5f);
                rightFace.lineTo(top.x + tileW * 0.5f, top.y + tileH);
                rightFace.closeSubPath();
                g.setColour(c.darker(0.15f));
                g.fillPath(rightFace);
            }
        }
    }

    buildCacheDirty = false;
}

void MainComponent::drawBuildOverlays(juce::Graphics& g)
{
    const float tileW = 64.0f;
    const float tileH = 32.0f;
    const juce::Point<float> origin = getBuildOrigin(tileW, tileH);

    auto drawCursor = [&](const PlayerCursor& p, const juce::String& label)
    {
        const int z = juce::jmax(0, p.z);
        const auto b00 = isoToScreen(p.x,     p.y,     0, tileW, tileH, origin, viewRotation);
        const auto b10 = isoToScreen(p.x + 1, p.y,     0, tileW, tileH, origin, viewRotation);
        const auto b11 = isoToScreen(p.x + 1, p.y + 1, 0, tileW, tileH, origin, viewRotation);
        const auto b01 = isoToScreen(p.x,     p.y + 1, 0, tileW, tileH, origin, viewRotation);

        const auto m00 = isoToScreen(p.x,     p.y,     z, tileW, tileH, origin, viewRotation);
        const auto m10 = isoToScreen(p.x + 1, p.y,     z, tileW, tileH, origin, viewRotation);
        const auto m11 = isoToScreen(p.x + 1, p.y + 1, z, tileW, tileH, origin, viewRotation);
        const auto m01 = isoToScreen(p.x,     p.y + 1, z, tileW, tileH, origin, viewRotation);

        juce::Path square;
        square.startNewSubPath(m00);
        square.lineTo(m10);
        square.lineTo(m11);
        square.lineTo(m01);
        square.closeSubPath();

        g.setColour(p.colour.withAlpha(0.18f + globalJuice * 0.35f));
        g.fillPath(square);
        g.setColour(p.colour.withAlpha(0.95f));
        g.strokePath(square, juce::PathStrokeType(2.6f));

        if (p.z > 0)
        {
            g.setColour(p.colour.withAlpha(0.72f));
            g.drawLine(b00.x, b00.y, m00.x, m00.y, 1.6f);
            g.drawLine(b10.x, b10.y, m10.x, m10.y, 1.6f);
            g.drawLine(b11.x, b11.y, m11.x, m11.y, 1.6f);
            g.drawLine(b01.x, b01.y, m01.x, m01.y, 1.6f);
        }

        const auto markerCenter = (m00 + m10 + m11 + m01) * 0.25f;
        if (p.z == 0)
        {
            g.setColour(p.colour.withAlpha(0.6f));
            g.drawLine(m00.x, m11.y, m11.x, m00.y, 1.3f);
        }

        g.setFont(juce::Font("Avenir Next", 16.0f, juce::Font::bold));
        g.drawText(label + " z" + juce::String(p.z), juce::Rectangle<int>(static_cast<int>(markerCenter.x) - 36, static_cast<int>(markerCenter.y) - 54, 84, 20), juce::Justification::centred);
    };

    drawCursor(p1, "P1");
    drawCursor(p2, "P2");

    for (const auto& p : pulses)
    {
        const float t = p.age / p.maxAge;
        const float r = (18.0f + 80.0f * p.size) * t;
        g.setColour(p.c.withAlpha((1.0f - t) * (0.4f + p.size * 0.45f)));
        g.drawEllipse(p.p.x - r, p.p.y - r, 2.0f * r, 2.0f * r, 2.2f);
    }

    g.setColour(juce::Colours::white.withAlpha(0.86f));
    g.setFont(juce::Font("Avenir Next", 17.0f, juce::Font::plain));
    g.drawText("BUILD MODE | P1 WASD Q/E R/F(place/remove @ z) | P2 Arrows [ ] / .(place/remove @ z) | View Z/X | K Key | G Scale | Y Quantize Blocks | H Beat | V Miverb | B Mix | Enter Performance",
               14, 10, getWidth() - 28, 24, juce::Justification::left);

    g.drawText("BPM " + juce::String(bpm, 1) + "  Key " + juce::MidiMessage::getMidiNoteName(keyRoot, true, true, 4).upToFirstOccurrenceOf(" ", false, false) +
                   "  Scale " + scaleToString(scale) +
                   "  LiveQuant " + juce::String(quantizeToScale ? "On(T)" : "Off(T)") +
                   "  Synth " + synthToString(synthEngine) +
                   "  Beat " + juce::String(beatStyleName(beatStyle)) +
                   "  Miverb " + juce::String((miverbEnabled && miverbMix > 0.001f) ? "On" : "Off") +
                   " " + juce::String(miverbMix, 2),
               14, 34, getWidth() - 28, 22, juce::Justification::left);

    if (selectingPerformanceMode)
    {
        auto panel = getLocalBounds().toFloat().withSizeKeepingCentre(540.0f, 220.0f);
        g.setColour(juce::Colour(0xee070b14));
        g.fillRoundedRectangle(panel, 14.0f);
        g.setColour(juce::Colour(0xff16e9ff).withAlpha(0.9f));
        g.drawRoundedRectangle(panel, 14.0f, 2.0f);

        g.setColour(juce::Colours::white.withAlpha(0.95f));
        g.setFont(juce::Font("Avenir Next", 28.0f, juce::Font::bold));
        g.drawText("Choose Performance Mode", panel.removeFromTop(58.0f).toNearestInt(), juce::Justification::centred);

        const juce::String labels[2] = { "Mode A  (Autonomous Snakes)", "Mode B  (Player-Steered Snakes)" };
        for (int i = 0; i < 2; ++i)
        {
            auto row = panel.removeFromTop(62.0f).reduced(26.0f, 8.0f);
            const bool sel = (i == pendingPerformanceModeIndex);
            g.setColour((sel ? juce::Colour(0xff16e9ff) : juce::Colours::white).withAlpha(sel ? 0.20f : 0.10f));
            g.fillRoundedRectangle(row, 10.0f);
            g.setColour((sel ? juce::Colour(0xff16e9ff) : juce::Colours::white).withAlpha(sel ? 0.95f : 0.65f));
            g.drawRoundedRectangle(row, 10.0f, sel ? 2.1f : 1.0f);
            g.setFont(juce::Font("Avenir Next", sel ? 22.0f : 20.0f, sel ? juce::Font::bold : juce::Font::plain));
            g.drawText(labels[i], row.toNearestInt(), juce::Justification::centred);
        }

        g.setFont(juce::Font("Avenir Next", 16.0f, juce::Font::plain));
        g.setColour(juce::Colours::white.withAlpha(0.72f));
        g.drawText("Up/Down to choose, Enter to confirm, Esc to cancel",
                   panel.withTrimmedTop(8.0f).toNearestInt(),
                   juce::Justification::centred);
    }
}

void MainComponent::paintBuild(juce::Graphics& g)
{
    updateBuildCache();
    const float shakeAmount = screenShake * 16.0f;
    const float dx = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    const float dy = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    g.addTransform(juce::AffineTransform::translation(dx, dy));
    g.drawImageAt(buildCache, 0, 0);
    drawBuildOverlays(g);
}

void MainComponent::updatePerformanceCache()
{
    if (! performanceCacheDirty && performanceCache.isValid() &&
        performanceCache.getWidth() == getWidth() && performanceCache.getHeight() == getHeight())
        return;

    performanceCache = juce::Image(juce::Image::RGB, getWidth(), getHeight(), true);
    juce::Graphics g(performanceCache);
    g.fillAll(juce::Colour(0xff090d14));

    const auto bounds = getLocalBounds().toFloat().reduced(80.0f, 80.0f);
    const float cell = juce::jmin(bounds.getWidth(), bounds.getHeight()) / static_cast<float>(worldSize);
    const juce::Point<float> origin(bounds.getCentreX() - cell * worldSize * 0.5f,
                                    bounds.getCentreY() - cell * worldSize * 0.5f);

    for (int y = 0; y < worldSize; ++y)
    {
        for (int x = 0; x < worldSize; ++x)
        {
            juce::Rectangle<float> r(origin.x + x * cell, origin.y + y * cell, cell, cell);
            const auto rr = r.reduced(1.0f);
            g.setColour(juce::Colour(0xff141b28));
            g.fillRect(rr);

            std::vector<int> stack;
            stack.reserve(maxHeight);
            for (int z = 1; z < maxHeight; ++z)
                if (hasBlock(x, y, z))
                    stack.push_back(z);

            if (! stack.empty())
            {
                const float bandH = rr.getHeight() / static_cast<float>(stack.size());
                for (size_t i = 0; i < stack.size(); ++i)
                {
                    // Lower notes are shown in lower bands.
                    const float yBand = rr.getY() + rr.getHeight() - bandH * static_cast<float>(i + 1);
                    juce::Rectangle<float> band(rr.getX(), yBand, rr.getWidth(), bandH);
                    g.setColour(pitchClassColour((stack[i] - 1) % 12).brighter(0.25f).withAlpha(0.60f));
                    g.fillRect(band);
                }
            }

            g.setColour(juce::Colour(0xff263041));
            g.drawRect(r, 1.0f);
        }
    }

    performanceCacheDirty = false;
}

void MainComponent::drawPerformanceOverlays(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced(80.0f, 80.0f);
    const float cell = juce::jmin(bounds.getWidth(), bounds.getHeight()) / static_cast<float>(worldSize);
    const juce::Point<float> origin(bounds.getCentreX() - cell * worldSize * 0.5f,
                                    bounds.getCentreY() - cell * worldSize * 0.5f);

    const auto toPixel = [&](juce::Point<int> c)
    {
        return juce::Point<float>(origin.x + (c.x + 0.5f) * cell, origin.y + (c.y + 0.5f) * cell);
    };

    for (const auto& pulse : pulses)
    {
        const float t = pulse.age / pulse.maxAge;
        const float radius = (8.0f + 52.0f * pulse.size) * t;
        g.setColour(pulse.c.withAlpha(0.85f * (1.0f - t)));
        g.drawEllipse(pulse.p.x - radius, pulse.p.y - radius, radius * 2.0f, radius * 2.0f, 2.0f + pulse.size * 2.2f);
        g.setColour(pulse.c.withAlpha(0.22f * (1.0f - t)));
        g.fillEllipse(pulse.p.x - radius * 0.33f, pulse.p.y - radius * 0.33f, radius * 0.66f * 2.0f, radius * 0.66f * 2.0f);
    }

    auto drawSnake = [&](const Snake& s)
    {
        if (s.trail.size() > 1)
        {
            juce::Path path;
            path.startNewSubPath(origin.x + (s.trail.front().x + 0.5f) * cell,
                                 origin.y + (s.trail.front().y + 0.5f) * cell);
            for (size_t i = 1; i < s.trail.size(); ++i)
                path.lineTo(origin.x + (s.trail[i].x + 0.5f) * cell,
                            origin.y + (s.trail[i].y + 0.5f) * cell);
            g.setColour(s.colour.withAlpha(0.18f + globalJuice * 0.45f));
            g.strokePath(path, juce::PathStrokeType(11.0f, juce::PathStrokeType::JointStyle::curved));
            g.setColour(s.colour.withAlpha(0.75f));
            g.strokePath(path, juce::PathStrokeType(4.0f, juce::PathStrokeType::JointStyle::curved));
        }

        const auto p = toPixel(s.cell);
        g.setColour(s.colour.withAlpha(0.95f));
        g.fillEllipse(p.x - cell * 0.28f, p.y - cell * 0.28f, cell * 0.56f, cell * 0.56f);
    };

    drawSnake(snakeA);
    drawSnake(snakeB);
    if (performanceMode == PerformanceMode::a && modeASnakeCount >= 3) drawSnake(snakeC);
    if (performanceMode == PerformanceMode::a && modeASnakeCount >= 4) drawSnake(snakeD);

    for (const auto& s : sparks)
    {
        const float t = s.age / s.maxAge;
        g.setColour(s.c.withAlpha((1.0f - t) * 0.95f));
        g.drawLine(s.p.x, s.p.y, s.p.x - s.v.x * 0.016f, s.p.y - s.v.y * 0.016f, juce::jmax(1.0f, s.size * (1.0f - t)));
    }

    auto drawMarker = [&](const PlayerCursor& p, const juce::String& label)
    {
        const juce::Point<float> c(origin.x + (p.x + 0.5f) * cell, origin.y + (p.y + 0.5f) * cell);
        g.setColour(p.colour.withAlpha(0.9f));
        g.drawEllipse(c.x - cell * 0.35f, c.y - cell * 0.35f, cell * 0.7f, cell * 0.7f, 2.0f);
        g.drawText(label, juce::Rectangle<int>(static_cast<int>(c.x - 18), static_cast<int>(c.y - 10), 36, 20), juce::Justification::centred);
    };

    drawMarker(p1, "P1");
    drawMarker(p2, "P2");

    for (int y = 0; y < worldSize; ++y)
    {
        for (int x = 0; x < worldSize; ++x)
        {
            const Tool& t = tools[x][y];
            if (t.type == ToolType::none)
                continue;

            juce::Rectangle<float> r(origin.x + x * cell, origin.y + y * cell, cell, cell);
            g.setColour(juce::Colours::white.withAlpha(0.8f));

            if (t.type == ToolType::redirect)
            {
                g.drawEllipse(r.reduced(cell * 0.2f), 2.0f);
                const juce::Point<float> c = r.getCentre();
                const float a = static_cast<float>(t.rotation) * (juce::MathConstants<float>::halfPi);
                g.drawArrow(juce::Line<float>(c, c + juce::Point<float>(std::cos(a), std::sin(a)) * (cell * 0.24f)), 2.2f, 8.0f, 8.0f);
            }
            else
            {
                g.setColour(juce::Colour(0x99ffffff));
                g.fillRoundedRectangle(r.reduced(cell * 0.2f), 4.0f);
                g.setColour(juce::Colour(0xff121923));
                juce::String label = "?";
                if (t.type == ToolType::speed) label = "SPD" + juce::String(t.state + 1);
                if (t.type == ToolType::ratchet) label = "R" + juce::String(t.state + 1);
                if (t.type == ToolType::key) label = "K" + juce::String(t.state);
                if (t.type == ToolType::scale) label = "S" + juce::String(t.state);
                if (t.type == ToolType::section) label = "SEC" + juce::String(t.state);
                g.setFont(juce::Font("Avenir Next", 11.0f, juce::Font::bold));
                g.drawText(label, r.toNearestInt(), juce::Justification::centred);
            }
        }
    }

    if (performanceMode == PerformanceMode::b)
    {
        for (int y = 0; y < worldSize; ++y)
        {
            for (int x = 0; x < worldSize; ++x)
            {
                if (! randomRedirectActive[x][y])
                    continue;

                juce::Rectangle<float> r(origin.x + x * cell, origin.y + y * cell, cell, cell);
                const juce::Point<float> c = r.getCentre();
                const float a = static_cast<float>(randomRedirectRotation[x][y] % 4) * juce::MathConstants<float>::halfPi;
                g.setColour(juce::Colour(0xff16e9ff).withAlpha(0.75f));
                g.drawEllipse(r.reduced(cell * 0.18f), 2.0f);
                g.drawArrow(juce::Line<float>(c, c + juce::Point<float>(std::cos(a), std::sin(a)) * (cell * 0.24f)), 2.0f, 7.0f, 7.0f);
            }
        }
    }

    if (barPulse)
    {
        g.setColour(juce::Colours::white.withAlpha(0.18f));
        const float rr = 80.0f + (beatPhase * 40.0f);
        g.drawEllipse(bounds.getCentreX() - rr, bounds.getCentreY() - rr, rr * 2.0f, rr * 2.0f, 2.0f);
    }
    g.setColour(juce::Colour(0xff00fff2).withAlpha(beatFlash * 0.22f));
    const float radarR = 40.0f + beatFlash * 420.0f;
    g.drawEllipse(bounds.getCentreX() - radarR, bounds.getCentreY() - radarR, radarR * 2.0f, radarR * 2.0f, 3.0f);

    g.setColour(juce::Colours::white.withAlpha(0.88f));
    g.setFont(juce::Font("Avenir Next", 17.0f, juce::Font::plain));
    const juce::String modeLabel = (performanceMode == PerformanceMode::a ? "PERFORMANCE MODE A" : "PERFORMANCE MODE B");
    const juce::String steerHint = (performanceMode == PerformanceMode::b
                                        ? " | Steer: P1 WASD P2 Arrows"
                                        : " | I: Add Snake (" + juce::String(modeASnakeCount) + "/4) | P: Switch A/B");
    g.drawText(modeLabel + steerHint +
                   " | Tool " + toolToString(static_cast<ToolType>(selectedToolIndex)) +
                   " | Play " + playModeToString(playMode) +
                   " | Sec " + juce::String(sectionShortName(activeArrangementSection())) +
                   " | P1 " + juce::String(p1Points) + " P2 " + juce::String(p2Points) +
                   " | BPM " + juce::String(bpm, 1) +
                   " | Key " + juce::String(keyRoot) +
                   " | Scale " + scaleToString(scale) +
                   " | Synth " + synthToString(synthEngine) +
                   " | Beat " + juce::String(beatStyleName(beatStyle)) +
                   " | Miverb " + juce::String((miverbEnabled && miverbMix > 0.001f) ? "On" : "Off") +
                   " " + juce::String(miverbMix, 2),
               16, 10, getWidth() - 32, 26, juce::Justification::left);

    if (countdownBeats > 0)
    {
        g.setColour(juce::Colours::white.withAlpha(0.96f));
        g.setFont(juce::Font("Avenir Next", 78.0f, juce::Font::bold));
        g.drawText(juce::String(countdownBeats), getLocalBounds(), juce::Justification::centred);
    }
}

void MainComponent::paintPerformance(juce::Graphics& g)
{
    updatePerformanceCache();
    const float shakeAmount = screenShake * 24.0f;
    const float dx = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    const float dy = (rng.nextFloat() * 2.0f - 1.0f) * shakeAmount;
    g.addTransform(juce::AffineTransform::translation(dx, dy));
    g.drawImageAt(performanceCache, 0, 0);
    drawPerformanceOverlays(g);
}

std::vector<MainComponent::MenuAction> MainComponent::getTitleMenu() const
{
    std::vector<MenuAction> menu;
    if (hasSession)
        menu.push_back(MenuAction::resume);
    menu.push_back(MenuAction::save);
    menu.push_back(MenuAction::load);
    menu.push_back(MenuAction::demo);
    menu.push_back(MenuAction::blank);
    menu.push_back(MenuAction::quit);
    return menu;
}

bool MainComponent::isTitleActionEnabled(MenuAction action) const
{
    if (action == MenuAction::save)
        return hasVisitedBuildMode;
    return true;
}

void MainComponent::performMenuAction(MenuAction action)
{
    addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xff16e9ff), 1.2f);
    if (action == MenuAction::resume)
    {
        hasVisitedBuildMode = true;
        mode = Mode::build;
    }
    else if (action == MenuAction::save)
    {
        saveWorldToFileAs();
        mode = Mode::title;
    }
    else if (action == MenuAction::load)
    {
        loadWorldFromFile();
        hasVisitedBuildMode = true;
        mode = Mode::build;
    }
    else if (action == MenuAction::demo)
    {
        loadDemoWorld();
        hasVisitedBuildMode = true;
        mode = Mode::build;
    }
    else if (action == MenuAction::blank)
    {
        beginBlankWorldSizeSelection();
        repaint();
        return;
    }
    else if (action == MenuAction::quit)
    {
        if (auto* app = juce::JUCEApplicationBase::getInstance())
            app->systemRequestedQuit();
        return;
    }

    hasSession = true;
    selectedMenu = 0;
    repaint();
}

void MainComponent::beginBlankWorldSizeSelection()
{
    selectingBlankWorldSize = true;
    blankWorldSignatureIndex = signatureIndexForWorldSize(worldSize);
}

void MainComponent::confirmBlankWorldSizeSelection()
{
    if (blankWorldSignatureIndex == 0) worldSize = 12;
    else if (blankWorldSignatureIndex == 1) worldSize = 16;
    else worldSize = 20;

    selectingBlankWorldSize = false;
    resetBlankWorld();
    hasVisitedBuildMode = true;
    mode = Mode::build;
    hasSession = true;
    selectedMenu = 0;
}

void MainComponent::enterBuildMode()
{
    mode = Mode::build;
    hasVisitedBuildMode = true;
    addJuiceAtCell(p1.x, p1.y, p1.colour, 0.8f);
    repaint();
}

void MainComponent::startPerformanceMode(PerformanceMode targetMode)
{
    mode = Mode::performance;
    performanceMode = targetMode;
    p1Points = 0;
    p2Points = 0;
    countdownBeats = 4;
    nextBeatLayerBeat = static_cast<double>(beatCounter) + beatPhase;
    performanceMoveAccumulator = 0.0;
    performanceStepCounter = 0;
    snakeA.trail.clear();
    snakeB.trail.clear();
    randomRedirectCells.clear();
    for (auto& col : randomRedirectActive)
        for (auto& v : col)
            v = false;
    for (auto& col : randomRedirectRotation)
        for (auto& v : col)
            v = 0;
    if (performanceMode == PerformanceMode::b)
    {
        p1.x = snakeA.cell.x; p1.y = snakeA.cell.y;
        p2.x = snakeB.cell.x; p2.y = snakeB.cell.y;
    }
    else
    {
        modeASnakeCount = 1;
    }
    addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xff00ffe5), 1.4f);
    repaint();
}

void MainComponent::triggerMidi(int midiNote, float velocity, float noteLengthSeconds)
{
    triggerMidiDelayed(midiNote, velocity, noteLengthSeconds, 0.0f);
}

void MainComponent::triggerMidiDelayed(int midiNote, float velocity, float noteLengthSeconds, float delaySeconds)
{
    if (midiNote < 0 || midiNote > 127)
        return;

    if (midiNote < 120)
    {
        midiNote = juce::jlimit(0, 127, quantizeMidiToScale(midiNote));
        midiNote = juce::jlimit(0, 119, midiNote + 12); // global +1 octave for melodic synth voices
    }

    delaySeconds = juce::jmax(0.0f, delaySeconds);
    noteLengthSeconds = juce::jmax(0.02f, noteLengthSeconds);

    if (delaySeconds <= 0.0001f)
    {
        const juce::ScopedLock sl(synthLock);
        synth.noteOn(1, midiNote, velocity);
        pendingNoteOffs.push_back({ midiNote, noteLengthSeconds });
        return;
    }

    pendingNoteOns.push_back({ midiNote, velocity, noteLengthSeconds, delaySeconds });
}

void MainComponent::triggerBeatMidi(int midiNote, float velocity)
{
    if (midiNote < 120 || midiNote > 123)
        return;

    const juce::ScopedLock sl(synthLock);
    beatSynth.noteOn(1, midiNote, velocity);
}

void MainComponent::addJuiceAtCell(int x, int y, juce::Colour c, float strength)
{
    globalJuice = juce::jmin(1.0f, globalJuice + strength * 0.35f);
    beatFlash = juce::jmin(1.0f, beatFlash + strength * 0.4f);
    const float shakeScale = (mode == Mode::build ? 0.35f : 1.0f);
    screenShake = juce::jmin(1.0f, screenShake + strength * 0.26f * shakeScale);

    juce::Point<float> p;
    if (mode == Mode::performance)
    {
        const auto bounds = getLocalBounds().toFloat().reduced(80.0f, 80.0f);
        const float cell = juce::jmin(bounds.getWidth(), bounds.getHeight()) / static_cast<float>(worldSize);
        const juce::Point<float> origin(bounds.getCentreX() - cell * worldSize * 0.5f,
                                        bounds.getCentreY() - cell * worldSize * 0.5f);
        p = { origin.x + (x + 0.5f) * cell, origin.y + (y + 0.5f) * cell };
    }
    else if (mode == Mode::build)
    {
        const float tileW = 64.0f;
        const float tileH = 32.0f;
        const juce::Point<float> origin = getBuildOrigin(tileW, tileH);
        p = isoToScreen(x, y, juce::jmax(0, topBlockZ(x, y)), tileW, tileH, origin, viewRotation);
    }
    else
    {
        p = getLocalBounds().toFloat().getCentre();
    }

    pulses.push_back({ p, 0.0f, 0.5f + strength * 0.28f, c, juce::jlimit(0.5f, 2.0f, strength * 1.5f) });
    for (int i = 0; i < 8; ++i)
    {
        const float a = rng.nextFloat() * juce::MathConstants<float>::twoPi;
        const float v = 80.0f + rng.nextFloat() * 320.0f * strength;
        sparks.push_back({ p,
                           { std::cos(a) * v, std::sin(a) * v },
                           0.0f,
                           0.24f + rng.nextFloat() * 0.35f,
                           1.5f + rng.nextFloat() * 3.5f * strength,
                           c.withBrightness(1.0f) });
    }
}

int MainComponent::triggerNoteForCell(int x, int y, float velocity, float noteLengthSeconds, int ratchet, int gateSection, int snakeRole, int forcedImprovStyle)
{
    if (x < 0 || y < 0 || x >= worldSize || y >= worldSize)
        return -1;

    if (tools[x][y].type == ToolType::section)
    {
        const int taggedSection = juce::jlimit(0, 4, tools[x][y].state);
        const int activeSection = (gateSection >= 0 ? gateSection : activeArrangementSection());
        if (arrangementEnabled && taggedSection != activeSection)
            return -1;
    }

    std::vector<int> stack;
    stack.reserve(maxHeight);
    for (int z = 1; z < maxHeight; ++z)
    {
        if (! hasBlock(x, y, z))
            continue;
        stack.push_back(zToMidi(z));
    }

    if (stack.empty())
        return -1;

    int section = activeArrangementSection();
    if (tools[x][y].type == ToolType::section)
        section = juce::jlimit(0, 4, tools[x][y].state);

    const int beatsPerBar = beatsPerBarForWorldSize(worldSize);
    const float beatInBar = static_cast<float>(beatCounter % beatsPerBar) + static_cast<float>(beatPhase);

    const auto applyRoleRegister = [&](int note) -> int
    {
        int out = note;
        if (snakeRole == 1)
        {
            // Snake B holds a bass register for stronger call/response contrast.
            while (out > 55)
                out -= 12;
        }
        else
        {
            // Snake A stays above bass to keep parts separated.
            while (out < 60)
                out += 12;
        }
        return juce::jlimit(24, 108, out);
    };

    const auto pickStackNote = [&]() -> int
    {
        const int n = static_cast<int>(stack.size());
        const int baseIdx = (beatCounter + barCounter * 3 + x * 2 + y + section * 5) % n;
        int idx = baseIdx;
        if (snakeRole == 1)
            idx = (n - 1 - baseIdx + n) % n; // mirror for complementary line

        int note = stack[static_cast<size_t>(idx)];
        if (snakeRole == 1 && n == 1)
            note += 7; // force a consonant contrast when no alternate stack note exists
        return applyRoleRegister(note);
    };

    const auto emitRatcheted = [&](int note, float baseVelocity, float baseLengthSeconds, int jumpSemitones, int repeatsOverride = -1)
    {
        const int repeats = juce::jlimit(1, 2, repeatsOverride > 0 ? repeatsOverride : ratchet);
        const float spacing = juce::jmax(0.012f, baseLengthSeconds / static_cast<float>(juce::jmax(1, repeats)));
        for (int i = 0; i < repeats; ++i)
        {
            int stepped = note;
            if (i > 0)
            {
                const int stepMul = (i % 2 == 1) ? 1 : 2;
                const int dir = ((x + y + barCounter + snakeRole + i) & 1) == 0 ? 1 : -1;
                stepped += dir * stepMul * jumpSemitones;
            }

            const float v = baseVelocity * juce::jmax(0.30f, 1.0f - 0.14f * static_cast<float>(i));
            const float l = juce::jmax(0.028f, spacing * 0.85f * (1.0f - 0.06f * static_cast<float>(i)));
            const float d = spacing * static_cast<float>(i);
            triggerMidiDelayed(applyRoleRegister(stepped), v, l, d);
        }
    };

    const auto triggerChordFromStack = [&](float velScale)
    {
        std::vector<int> chosen;
        chosen.reserve(4);

        if (snakeRole == 0)
        {
            for (int i = static_cast<int>(stack.size()) - 1; i >= 0 && static_cast<int>(chosen.size()) < 4; --i)
                chosen.push_back(stack[static_cast<size_t>(i)]);
        }
        else
        {
            for (int i = 0; i < static_cast<int>(stack.size()) && static_cast<int>(chosen.size()) < 4; ++i)
                chosen.push_back(stack[static_cast<size_t>(i)]);
        }

        if (chosen.size() == 1)
        {
            chosen.push_back(chosen[0] + 4);
            chosen.push_back(chosen[0] + 7);
        }
        else if (chosen.size() == 2)
        {
            chosen.push_back(chosen[0] + 7);
        }

        const float secondsPerBeat = 60.0f / static_cast<float>(juce::jmax(1.0, bpm));
        const float beatsUntilFinalBeatStart = juce::jmax(0.0f, static_cast<float>(beatsPerBar - 1) - beatInBar);
        const float gateUntilBeat4Seconds = juce::jmax(0.01f, beatsUntilFinalBeatStart * secondsPerBeat);
        const auto latchGuard = juce::ScopedValueSetter<bool>(chordLatchMode, true);

        for (size_t i = 0; i < chosen.size(); ++i)
        {
            const float vv = velocity * velScale * (0.98f - static_cast<float>(i) * 0.12f);
            triggerMidi(applyRoleRegister(chosen[i]), vv, gateUntilBeat4Seconds);
        }
    };

    const auto triggerArpFromStack = [&]()
    {
        std::vector<int> arp = stack;
        std::sort(arp.begin(), arp.end());

        const int directionMode = (barCounter + x * 2 + y * 3 + section) % 3;
        const int appliedDirection = (snakeRole == 1 ? (directionMode + 1) % 3 : directionMode); // opposite tendency
        if (appliedDirection == 1)
        {
            std::reverse(arp.begin(), arp.end());
        }
        else if (appliedDirection == 2 && arp.size() >= 3)
        {
            for (int i = static_cast<int>(arp.size()) - 2; i > 0; --i)
                arp.push_back(arp[static_cast<size_t>(i)]);
        }

        const int idx = (beatCounter + x + y + snakeRole) % static_cast<int>(arp.size());
        const int note = arp[static_cast<size_t>(idx)];
        emitRatcheted(note, velocity, noteLengthSeconds, snakeRole == 1 ? 5 : 7);

        if (arp.size() > 1)
        {
            const int next = arp[static_cast<size_t>((idx + 1) % static_cast<int>(arp.size()))];
            emitRatcheted(next, velocity * 0.72f, noteLengthSeconds * 0.72f, snakeRole == 1 ? 5 : 7, juce::jmax(1, ratchet - 1));
        }
    };

    // Always improvise on stack hits: choose single/chord/arp musically.
    enum class ImprovStyle { single, chord, arp };
    ImprovStyle style = ImprovStyle::single;
    if (stack.size() > 1)
    {
        int singleW = 56;
        int chordW = 30;
        int arpW = 14;

        switch (section)
        {
            case 0: singleW = 74; chordW = 22; arpW = 4; break;
            case 1: singleW = 56; chordW = 30; arpW = 14; break;
            case 2: singleW = 40; chordW = 35; arpW = 25; break;
            case 3: singleW = 28; chordW = 48; arpW = 24; break;
            case 4: singleW = 36; chordW = 24; arpW = 40; break;
            default: break;
        }

        const int beatSlot = juce::jlimit(0, beatsPerBar - 1, static_cast<int>(std::floor(beatInBar)));
        if (beatSlot == 0)
        {
            singleW += 10;
            arpW -= 8;
        }
        else if (beatSlot == (beatsPerBar - 1))
        {
            singleW -= 10;
            chordW += 6;
            arpW += 8;
        }
        else if (beatSlot == 1 && section >= 2)
        {
            singleW -= 4;
            arpW += 6;
        }

        // Keep play mode relevant as a bias, not a hard lock.
        if (playMode == PlayMode::chord)
        {
            chordW += 22;
            singleW -= 12;
            arpW -= 10;
        }
        else if (playMode == PlayMode::arpeggio)
        {
            arpW += 24;
            singleW -= 12;
            chordW -= 8;
        }

        singleW = juce::jmax(5, singleW);
        chordW = juce::jmax(5, chordW);
        arpW = juce::jmax(5, arpW);
        const int total = singleW + chordW + arpW;

        if (forcedImprovStyle >= 0 && forcedImprovStyle <= 2)
        {
            style = static_cast<ImprovStyle>(forcedImprovStyle);
        }
        else
        {
            const int pick = (beatCounter * 11 + barCounter * 17 + x * 13 + y * 19
                            + static_cast<int>(stack.size()) * 23 + section * 29 + snakeRole * 31) % total;
            if (pick < singleW) style = ImprovStyle::single;
            else if (pick < (singleW + chordW)) style = ImprovStyle::chord;
            else style = ImprovStyle::arp;
        }
    }

    if (style == ImprovStyle::single)
    {
        const int note = pickStackNote();
        emitRatcheted(note, velocity, noteLengthSeconds, snakeRole == 1 ? 7 : 12);
    }
    else if (style == ImprovStyle::chord)
    {
        triggerChordFromStack(0.78f);
    }
    else
    {
        triggerArpFromStack();
    }

    const int top = topBlockZ(x, y);
    addJuiceAtCell(x, y, pitchClassColour((juce::jmax(1, top) - 1) % 12), juce::jlimit(0.4f, 1.5f, velocity * (0.8f + 0.2f * ratchet)));
    return static_cast<int>(style);
}

void MainComponent::applyToolToSnake(Snake& snake)
{
    if (performanceMode == PerformanceMode::b && randomRedirectActive[snake.cell.x][snake.cell.y])
    {
        static constexpr juce::Point<int> dirs[4] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
        snake.dir = dirs[(randomRedirectRotation[snake.cell.x][snake.cell.y] % 4 + 4) % 4];
    }

    Tool& t = tools[snake.cell.x][snake.cell.y];

    switch (t.type)
    {
        case ToolType::none:
            break;
        case ToolType::redirect:
        {
            static constexpr juce::Point<int> dirs[4] = { { 1, 0 }, { 0, 1 }, { -1, 0 }, { 0, -1 } };
            snake.dir = dirs[(t.rotation % 4 + 4) % 4];
            break;
        }
        case ToolType::speed:
            snake.speedState = juce::jlimit(0, 2, t.state);
            break;
        case ToolType::ratchet:
            snake.ratchet = juce::jlimit(1, 2, t.state + 1);
            break;
        case ToolType::key:
            keyRoot = juce::jlimit(0, 11, t.state);
            break;
        case ToolType::scale:
            scale = static_cast<ScaleType>(juce::jlimit(0, 4, t.state));
            break;
        case ToolType::section:
        {
            const int sec = juce::jlimit(0, 4, t.state);
            arrangementEnabled = true;
            arrangementSequenceIndex = arrangementIndexForSection(sec);
            arrangementBarsInSection = 0;

            switch (sec)
            {
                case 0: playMode = PlayMode::melodic; synthEngine = SynthEngine::digitalV4; break;
                case 1: playMode = PlayMode::melodic; synthEngine = SynthEngine::fmGlass; break;
                case 2: playMode = PlayMode::arpeggio; synthEngine = SynthEngine::fmGlass; break;
                case 3: playMode = PlayMode::chord; synthEngine = SynthEngine::velvetNoise; break;
                case 4: playMode = PlayMode::melodic; synthEngine = SynthEngine::velvetNoise; break;
                default: break;
            }
            break;
        }
    }
}

void MainComponent::advancePerformanceStep()
{
    if (performanceMode == PerformanceMode::b)
    {
        p1.x = snakeA.cell.x; p1.y = snakeA.cell.y;
        p2.x = snakeB.cell.x; p2.y = snakeB.cell.y;
    }

    ++performanceStepCounter;
    if (performanceMode == PerformanceMode::b)
    {
        const auto clearRandomCell = [&](const juce::Point<int>& c)
        {
            if (! randomRedirectActive[c.x][c.y])
                return;
            randomRedirectActive[c.x][c.y] = false;
            randomRedirectRotation[c.x][c.y] = 0;
        };

        if ((performanceStepCounter % 2) == 0)
        {
            if (rng.nextFloat() < 0.32f)
            {
                for (int tries = 0; tries < 12; ++tries)
                {
                    const int x = rng.nextInt(worldSize);
                    const int y = rng.nextInt(worldSize);
                    if (randomRedirectActive[x][y])
                        continue;
                    if (tools[x][y].type != ToolType::none)
                        continue;
                    if ((x == snakeA.cell.x && y == snakeA.cell.y) || (x == snakeB.cell.x && y == snakeB.cell.y))
                        continue;
                    randomRedirectActive[x][y] = true;
                    randomRedirectRotation[x][y] = rng.nextInt(4);
                    randomRedirectCells.push_back({ x, y });
                    performanceCacheDirty = true;
                    break;
                }
            }

            if (! randomRedirectCells.empty() && rng.nextFloat() < 0.26f)
            {
                const int idx = rng.nextInt(static_cast<int>(randomRedirectCells.size()));
                const auto c = randomRedirectCells[static_cast<size_t>(idx)];
                clearRandomCell(c);
                randomRedirectCells.erase(randomRedirectCells.begin() + idx);
                performanceCacheDirty = true;
            }
        }
    }

    auto stepSnake = [&](Snake& s, Snake& other, int snakeRole, int forcedImprovStyle) -> int
    {
        const int divisor = s.speedState == 0 ? 4 : s.speedState == 1 ? 2 : 1;
        if ((performanceStepCounter % divisor) != 0)
            return -1;

        auto next = s.cell + s.dir;
        if (next.x < 0 || next.x >= worldSize)
        {
            s.dir.x = -s.dir.x;
            next = s.cell + s.dir;
        }
        if (next.y < 0 || next.y >= worldSize)
        {
            s.dir.y = -s.dir.y;
            next = s.cell + s.dir;
        }

        if (performanceMode == PerformanceMode::a &&
            ((next.x == p1.x && next.y == p1.y) || (next.x == p2.x && next.y == p2.y)))
        {
            s.dir = { -s.dir.x, -s.dir.y };
            next = s.cell + s.dir;
            next.x = juce::jlimit(0, worldSize - 1, next.x);
            next.y = juce::jlimit(0, worldSize - 1, next.y);
        }

        if (performanceMode == PerformanceMode::b && next == other.cell)
        {
            if (snakeRole == 0) p1Points = 0;
            else p2Points = 0;

            s.dir = { -s.dir.x, -s.dir.y };
            next = s.cell + s.dir;
            next.x = juce::jlimit(0, worldSize - 1, next.x);
            next.y = juce::jlimit(0, worldSize - 1, next.y);
            addJuiceAtCell(s.cell.x, s.cell.y, juce::Colour(0xffff3a6e), 1.1f);
        }

        s.cell = next;
        s.trail.push_back({ static_cast<float>(s.cell.x), static_cast<float>(s.cell.y) });
        if (s.trail.size() > 22)
            s.trail.erase(s.trail.begin());

        const int stepSection = activeArrangementSection();
        applyToolToSnake(s);
        const int chosenStyle = triggerNoteForCell(s.cell.x, s.cell.y, 0.9f, 0.18f, s.ratchet, stepSection, snakeRole, forcedImprovStyle);
        if (chosenStyle >= 0)
        {
            int complexity = 0;
            for (int z = 1; z < maxHeight; ++z)
                if (hasBlock(s.cell.x, s.cell.y, z))
                    ++complexity;

            if (complexity > 0)
            {
                const bool onBeat = (performanceStepCounter % 4) == 0;
                const int reward = complexity * (onBeat ? 20 : 10);
                if (snakeRole == 0) p1Points += reward;
                else p2Points += reward;
            }
        }

        const auto bounds = getLocalBounds().toFloat().reduced(80.0f, 80.0f);
        const float cell = juce::jmin(bounds.getWidth(), bounds.getHeight()) / static_cast<float>(worldSize);
        const juce::Point<float> origin(bounds.getCentreX() - cell * worldSize * 0.5f,
                                        bounds.getCentreY() - cell * worldSize * 0.5f);
        pulses.push_back({ { origin.x + (s.cell.x + 0.5f) * cell, origin.y + (s.cell.y + 0.5f) * cell }, 0.0f, 0.4f, s.colour, 1.0f + s.ratchet * 0.24f });
        return chosenStyle;
    };

    if (performanceMode == PerformanceMode::a)
    {
        std::array<Snake*, 4> snakes { &snakeA, &snakeB, &snakeC, &snakeD };
        int previousStyle = -1;
        for (int i = 0; i < modeASnakeCount; ++i)
        {
            int forcedStyle = -1;
            if (previousStyle >= 0)
                forcedStyle = (previousStyle + 1 + ((performanceStepCounter + barCounter + i) & 1)) % 3;
            const int chosen = stepSnake(*snakes[static_cast<size_t>(i)],
                                         *snakes[static_cast<size_t>((i + 1) % juce::jmax(1, modeASnakeCount))],
                                         i % 2,
                                         forcedStyle);
            if (chosen >= 0)
                previousStyle = chosen;
        }
    }
    else
    {
        const int styleA = stepSnake(snakeA, snakeB, 0, -1);
        int forcedB = -1;
        if (styleA >= 0)
            forcedB = (styleA + 1 + ((performanceStepCounter + barCounter) & 1)) % 3;
        stepSnake(snakeB, snakeA, 1, forcedB);
    }
}

void MainComponent::advanceTransport(double deltaSeconds)
{
    const double previousTotalBeats = static_cast<double>(beatCounter) + beatPhase;
    const double beatsPerSec = bpm / 60.0;
    const double deltaBeats = deltaSeconds * beatsPerSec;
    beatPhase += deltaBeats;

    const double currentTotalBeats = static_cast<double>(beatCounter) + beatPhase;
    if (mode == Mode::performance)
    {
        if (nextBeatLayerBeat < previousTotalBeats)
            nextBeatLayerBeat = previousTotalBeats;

        int guard = 0;
        while (nextBeatLayerBeat <= currentTotalBeats && guard++ < 256)
        {
            triggerBeatLayer(nextBeatLayerBeat);
            nextBeatLayerBeat += 0.25; // 16th grid
        }
    }

    while (beatPhase >= 1.0)
    {
        beatPhase -= 1.0;
        ++beatCounter;
        const int beatsPerBar = beatsPerBarForWorldSize(worldSize);
        barPulse = (beatCounter % beatsPerBar) == 0;
        if (barPulse)
        {
            ++barCounter;
            advanceArrangementBar();
            beatFlash = 1.0f;
            screenShake = juce::jmin(1.0f, screenShake + 0.08f);
        }

        if (mode == Mode::performance && countdownBeats > 0)
            --countdownBeats;
    }

    if (mode == Mode::performance && countdownBeats <= 0)
    {
        performanceMoveAccumulator += deltaBeats;
        while (performanceMoveAccumulator >= 0.25)
        {
            performanceMoveAccumulator -= 0.25;
            advancePerformanceStep();
        }
    }
}

void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes();
    const double delta = juce::jlimit(0.0, 0.2, (now - lastTimerMs) * 0.001);
    lastTimerMs = now;
    elapsedSinceTick += delta;
    titlePhase += static_cast<float>(delta * 2.4);

    advanceTransport(delta);

    for (auto it = pendingNoteOns.begin(); it != pendingNoteOns.end();)
    {
        it->secondsRemaining -= static_cast<float>(delta);
        if (it->secondsRemaining <= 0.0f)
        {
            const juce::ScopedLock sl(synthLock);
            synth.noteOn(1, it->note, it->velocity);
            pendingNoteOffs.push_back({ it->note, it->lengthSeconds });
            it = pendingNoteOns.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = pendingNoteOffs.begin(); it != pendingNoteOffs.end();)
    {
        it->secondsRemaining -= static_cast<float>(delta);
        if (it->secondsRemaining <= 0.0f)
        {
            const juce::ScopedLock sl(synthLock);
            synth.noteOff(1, it->note, 0.0f, true);
            it = pendingNoteOffs.erase(it);
        }
        else
        {
            ++it;
        }
    }

    for (auto it = pulses.begin(); it != pulses.end();)
    {
        it->age += static_cast<float>(delta);
        if (it->age > it->maxAge)
            it = pulses.erase(it);
        else
            ++it;
    }

    for (auto it = sparks.begin(); it != sparks.end();)
    {
        it->age += static_cast<float>(delta);
        it->p += it->v * static_cast<float>(delta);
        it->v *= 0.94f;
        if (it->age > it->maxAge)
            it = sparks.erase(it);
        else
            ++it;
    }

    while (pulses.size() > 300)
        pulses.erase(pulses.begin());
    while (sparks.size() > 1200)
        sparks.erase(sparks.begin(), sparks.begin() + static_cast<long>(sparks.size() - 1200));

    globalJuice = juce::jmax(0.0f, globalJuice - static_cast<float>(delta * 1.7));
    beatFlash = juce::jmax(0.0f, beatFlash - static_cast<float>(delta * 2.8));
    screenShake = juce::jmax(0.0f, screenShake - static_cast<float>(delta * 3.2));

    if (mode == Mode::title)
        titleCacheDirty = true;

    repaint();
}

bool MainComponent::handleTitleKeys(const juce::KeyPress& key)
{
    if (selectingBlankWorldSize)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            selectingBlankWorldSize = false;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::upKey)
        {
            blankWorldSignatureIndex = (blankWorldSignatureIndex + 2) % 3;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::downKey)
        {
            blankWorldSignatureIndex = (blankWorldSignatureIndex + 1) % 3;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            confirmBlankWorldSizeSelection();
            repaint();
            return true;
        }
        return false;
    }

    const auto menu = getTitleMenu();
    selectedMenu = juce::jlimit(0, juce::jmax(0, static_cast<int>(menu.size()) - 1), selectedMenu);
    const auto isEnabledAt = [&](int idx)
    {
        return idx >= 0 && idx < static_cast<int>(menu.size()) && isTitleActionEnabled(menu[static_cast<size_t>(idx)]);
    };
    const auto advanceSelection = [&](int dir)
    {
        if (menu.empty())
            return;
        int next = selectedMenu;
        for (size_t n = 0; n < menu.size(); ++n)
        {
            next = (next + dir + static_cast<int>(menu.size())) % static_cast<int>(menu.size());
            if (isEnabledAt(next))
            {
                selectedMenu = next;
                return;
            }
        }
    };
    if (! isEnabledAt(selectedMenu))
        advanceSelection(1);

    if (key == juce::KeyPress::upKey)
    {
        advanceSelection(-1);
        addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xffff2c95), 0.45f);
        return true;
    }
    if (key == juce::KeyPress::downKey)
    {
        advanceSelection(1);
        addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xff16e9ff), 0.45f);
        return true;
    }
    if (key == juce::KeyPress::returnKey)
    {
        if (isEnabledAt(selectedMenu))
            performMenuAction(menu[static_cast<size_t>(selectedMenu)]);
        return true;
    }
    return false;
}

bool MainComponent::handleBuildKeys(const juce::KeyPress& key)
{
    if (selectingPerformanceMode)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            selectingPerformanceMode = false;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::upKey || key == juce::KeyPress::downKey)
        {
            pendingPerformanceModeIndex = 1 - pendingPerformanceModeIndex;
            repaint();
            return true;
        }
        if (key == juce::KeyPress::returnKey)
        {
            selectingPerformanceMode = false;
            startPerformanceMode(pendingPerformanceModeIndex == 0 ? PerformanceMode::a : PerformanceMode::b);
            repaint();
            return true;
        }
        return false;
    }

    auto moveCursor = [this](PlayerCursor& p, int dx, int dy)
    {
        p.x = juce::jlimit(0, worldSize - 1, p.x + dx);
        p.y = juce::jlimit(0, worldSize - 1, p.y + dy);
    };

    const juce::juce_wchar c = key.getTextCharacter();
    bool changedWorld = false;

    if (key == juce::KeyPress::escapeKey)
    {
        mode = Mode::title;
    }
    else if (key == juce::KeyPress::leftKey) moveCursor(p2, -1, 0);
    else if (key == juce::KeyPress::rightKey) moveCursor(p2, 1, 0);
    else if (key == juce::KeyPress::upKey) moveCursor(p2, 0, -1);
    else if (key == juce::KeyPress::downKey) moveCursor(p2, 0, 1);
    else if (c == 'w' || c == 'W') moveCursor(p1, 0, -1);
    else if (c == 's' || c == 'S') moveCursor(p1, 0, 1);
    else if (c == 'a' || c == 'A') moveCursor(p1, -1, 0);
    else if (c == 'd' || c == 'D') moveCursor(p1, 1, 0);
    else if (c == 'q' || c == 'Q') p1.z = juce::jlimit(0, maxHeight - 1, p1.z - 1);
    else if (c == 'e' || c == 'E') p1.z = juce::jlimit(0, maxHeight - 1, p1.z + 1);
    else if (c == '[') p2.z = juce::jlimit(0, maxHeight - 1, p2.z - 1);
    else if (c == ']') p2.z = juce::jlimit(0, maxHeight - 1, p2.z + 1);
    else if (c == 'z' || c == 'Z') { viewRotation = (viewRotation + 3) % 4; buildCacheDirty = true; }
    else if (c == 'x' || c == 'X') { viewRotation = (viewRotation + 1) % 4; buildCacheDirty = true; }
    else if (c == 'r' || c == 'R')
    {
        if (p1.z > 0)
        {
            const bool was = hasBlock(p1.x, p1.y, p1.z);
            setBlock(p1.x, p1.y, p1.z, true);
            changedWorld = changedWorld || ! was;
            triggerMidi(zToMidi(p1.z), 0.95f, 0.18f);
        }
    }
    else if (c == 'f' || c == 'F')
    {
        const bool was = hasBlock(p1.x, p1.y, p1.z);
        setBlock(p1.x, p1.y, p1.z, false);
        changedWorld = changedWorld || was;
    }
    else if (c == '/')
    {
        if (p2.z > 0)
        {
            const bool was = hasBlock(p2.x, p2.y, p2.z);
            setBlock(p2.x, p2.y, p2.z, true);
            changedWorld = changedWorld || ! was;
            triggerMidi(zToMidi(p2.z), 0.95f, 0.18f);
        }
    }
    else if (c == '.')
    {
        const bool was = hasBlock(p2.x, p2.y, p2.z);
        setBlock(p2.x, p2.y, p2.z, false);
        changedWorld = changedWorld || was;
    }
    else if (key == juce::KeyPress::returnKey)
    {
        selectingPerformanceMode = true;
        pendingPerformanceModeIndex = 0;
    }
    else if (c == '-')
    {
        bpm = juce::jlimit(50.0, 220.0, bpm - 2.0);
    }
    else if (c == '=')
    {
        bpm = juce::jlimit(50.0, 220.0, bpm + 2.0);
    }
    else if (c == 'k' || c == 'K')
    {
        keyRoot = (keyRoot + 1) % 12;
    }
    else if (c == 'g' || c == 'G')
    {
        scale = nextScale(scale);
    }
    else if (c == 'y' || c == 'Y')
    {
        changedWorld = quantizeWorldToCurrentScale() || changedWorld;
        if (changedWorld)
            addJuiceAtCell(p1.x, p1.y, p1.colour, 1.3f);
    }
    else if (c == 't' || c == 'T')
    {
        quantizeToScale = ! quantizeToScale;
    }
    else if (c == 'n' || c == 'N')
    {
        synthEngine = nextSynth(synthEngine);
    }
    else if (c == 'v' || c == 'V')
    {
        if (miverbEnabled && miverbMix > 0.001f)
        {
            miverbEnabled = false;
            miverbMix = 0.00f;
        }
        else
        {
            miverbEnabled = true;
            if (miverbMix <= 0.001f)
                miverbMix = 0.16f;
        }
    }
    else if (c == 'b' || c == 'B')
    {
        if (miverbMix < 0.08f) miverbMix = 0.16f;
        else if (miverbMix < 0.22f) miverbMix = 0.28f;
        else if (miverbMix < 0.34f) miverbMix = 0.40f;
        else if (miverbMix < 0.48f) miverbMix = 0.56f;
        else miverbMix = 0.00f;
        miverbEnabled = miverbMix > 0.001f;
    }
    else if (c == 'h' || c == 'H')
    {
        beatStyle = (beatStyle + 1) % 5;
    }
    else if (c == 'm' || c == 'M')
    {
        playMode = nextPlayMode(playMode);
    }
    else if (c == 'o' || c == 'O')
    {
        saveWorldToFile();
    }
    else if (c == 'u' || c == 'U')
    {
        mode = Mode::title;
    }
    else
    {
        return false;
    }

    if (changedWorld)
    {
        buildCacheDirty = true;
        performanceCacheDirty = true;
        addJuiceAtCell(p1.x, p1.y, p1.colour, 0.85f);
        addJuiceAtCell(p2.x, p2.y, p2.colour, 0.85f);
    }

    hasSession = true;
    repaint();
    return true;
}

bool MainComponent::handlePerformanceKeys(const juce::KeyPress& key)
{
    auto moveCursor = [this](PlayerCursor& p, int dx, int dy)
    {
        p.x = juce::jlimit(0, worldSize - 1, p.x + dx);
        p.y = juce::jlimit(0, worldSize - 1, p.y + dy);
    };

    const juce::juce_wchar c = key.getTextCharacter();

    if (key == juce::KeyPress::escapeKey)
    {
        enterBuildMode();
        return true;
    }

    bool handledSteer = false;
    bool movedMarker = false;
    if (performanceMode == PerformanceMode::b)
    {
        if (key == juce::KeyPress::leftKey) { snakeB.dir = { -1, 0 }; handledSteer = true; }
        else if (key == juce::KeyPress::rightKey) { snakeB.dir = { 1, 0 }; handledSteer = true; }
        else if (key == juce::KeyPress::upKey) { snakeB.dir = { 0, -1 }; handledSteer = true; }
        else if (key == juce::KeyPress::downKey) { snakeB.dir = { 0, 1 }; handledSteer = true; }
        else if (c == 'w' || c == 'W') { snakeA.dir = { 0, -1 }; handledSteer = true; }
        else if (c == 's' || c == 'S') { snakeA.dir = { 0, 1 }; handledSteer = true; }
        else if (c == 'a' || c == 'A') { snakeA.dir = { -1, 0 }; handledSteer = true; }
        else if (c == 'd' || c == 'D') { snakeA.dir = { 1, 0 }; handledSteer = true; }

        if (handledSteer)
        {
            p1.x = snakeA.cell.x; p1.y = snakeA.cell.y;
            p2.x = snakeB.cell.x; p2.y = snakeB.cell.y;
            addJuiceAtCell(snakeA.cell.x, snakeA.cell.y, snakeA.colour, 0.24f);
            addJuiceAtCell(snakeB.cell.x, snakeB.cell.y, snakeB.colour, 0.24f);
            repaint();
            return true;
        }
    }
    else
    {
        if (key == juce::KeyPress::leftKey) { moveCursor(p2, -1, 0); movedMarker = true; }
        else if (key == juce::KeyPress::rightKey) { moveCursor(p2, 1, 0); movedMarker = true; }
        else if (key == juce::KeyPress::upKey) { moveCursor(p2, 0, -1); movedMarker = true; }
        else if (key == juce::KeyPress::downKey) { moveCursor(p2, 0, 1); movedMarker = true; }
        else if (c == 'w' || c == 'W') { moveCursor(p1, 0, -1); movedMarker = true; }
        else if (c == 's' || c == 'S') { moveCursor(p1, 0, 1); movedMarker = true; }
        else if (c == 'a' || c == 'A') { moveCursor(p1, -1, 0); movedMarker = true; }
        else if (c == 'd' || c == 'D') { moveCursor(p1, 1, 0); movedMarker = true; }
    }

    if (movedMarker)
    {
        repaint();
        return true;
    }

    if (key == juce::KeyPress::tabKey)
    {
        selectedToolIndex = (selectedToolIndex % 6) + 1;
    }
    else if (c == 'r' || c == 'R' || c == '/')
    {
        PlayerCursor& actor = (c == '/' ? p2 : p1);
        Tool& t = tools[actor.x][actor.y];
        const ToolType selected = static_cast<ToolType>(selectedToolIndex);
        if (t.type != selected)
            t = { selected, 0, 0 };
        else if (t.type == ToolType::redirect)
            t.rotation = (t.rotation + 1) % 4;
        else if (t.type == ToolType::speed)
            t.state = (t.state + 1) % 3;
        else if (t.type == ToolType::ratchet)
            t.state = (t.state + 1) % 2;
        else if (t.type == ToolType::key)
            t.state = (t.state + 1) % 12;
        else if (t.type == ToolType::scale)
            t.state = (t.state + 1) % 5;
        else if (t.type == ToolType::section)
            t.state = (t.state + 1) % 5;

        performanceCacheDirty = true;
        addJuiceAtCell(actor.x, actor.y, actor.colour, 1.0f);
    }
    else if (c == 'f' || c == 'F' || c == '.')
    {
        PlayerCursor& actor = (c == '.' ? p2 : p1);
        Tool& t = tools[actor.x][actor.y];
        if (t.type != ToolType::none)
            t.rotation = (t.rotation + 1) % 4;
        performanceCacheDirty = true;
        addJuiceAtCell(actor.x, actor.y, actor.colour, 0.55f);
    }
    else if (c == 'm' || c == 'M')
    {
        playMode = nextPlayMode(playMode);
    }
    else if (c == 'p' || c == 'P')
    {
        performanceMode = (performanceMode == PerformanceMode::a ? PerformanceMode::b : PerformanceMode::a);
        if (performanceMode == PerformanceMode::b)
        {
            p1.x = snakeA.cell.x; p1.y = snakeA.cell.y;
            p2.x = snakeB.cell.x; p2.y = snakeB.cell.y;
        }
        else
        {
            randomRedirectCells.clear();
            for (auto& col : randomRedirectActive)
                for (auto& v : col)
                    v = false;
            for (auto& col : randomRedirectRotation)
                for (auto& v : col)
                    v = 0;
        }
        addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xff00fff2), 0.9f);
    }
    else if ((c == 'i' || c == 'I') && performanceMode == PerformanceMode::a)
    {
        modeASnakeCount = juce::jmin(4, modeASnakeCount + 1);
        addJuiceAtCell(worldSize / 2, worldSize / 2, juce::Colour(0xff9d7bff), 0.95f);
    }
    else if (c == 'n' || c == 'N')
    {
        synthEngine = nextSynth(synthEngine);
    }
    else if (c == 'v' || c == 'V')
    {
        if (miverbEnabled && miverbMix > 0.001f)
        {
            miverbEnabled = false;
            miverbMix = 0.00f;
        }
        else
        {
            miverbEnabled = true;
            if (miverbMix <= 0.001f)
                miverbMix = 0.16f;
        }
    }
    else if (c == 'b' || c == 'B')
    {
        if (miverbMix < 0.08f) miverbMix = 0.16f;
        else if (miverbMix < 0.22f) miverbMix = 0.28f;
        else if (miverbMix < 0.34f) miverbMix = 0.40f;
        else if (miverbMix < 0.48f) miverbMix = 0.56f;
        else miverbMix = 0.00f;
        miverbEnabled = miverbMix > 0.001f;
    }
    else if (c == 'h' || c == 'H')
    {
        beatStyle = (beatStyle + 1) % 5;
    }
    else if (c == 'k' || c == 'K')
    {
        keyRoot = (keyRoot + 11) % 12;
    }
    else if (c == 'l' || c == 'L')
    {
        keyRoot = (keyRoot + 1) % 12;
    }
    else if (c == 'g' || c == 'G')
    {
        scale = nextScale(scale);
    }
    else if (c == '-')
    {
        bpm = juce::jlimit(50.0, 220.0, bpm - 2.0);
    }
    else if (c == '=')
    {
        bpm = juce::jlimit(50.0, 220.0, bpm + 2.0);
    }
    else if (c == 'u' || c == 'U')
    {
        mode = Mode::title;
    }
    else
    {
        return false;
    }

    repaint();
    return true;
}

bool MainComponent::keyPressed(const juce::KeyPress& key, juce::Component*)
{
    const auto mods = key.getModifiers();
    const auto ch = key.getTextCharacter();
    if (mods.isCommandDown() && (ch == 's' || ch == 'S' || key.getKeyCode() == 's' || key.getKeyCode() == 'S'))
    {
        saveWorldToFileAs();
        return true;
    }

    if (mode == Mode::title)
        return handleTitleKeys(key);
    if (mode == Mode::build)
        return handleBuildKeys(key);
    return handlePerformanceKeys(key);
}

void MainComponent::saveWorldToFile()
{
    writeWorldToFile(worldSaveFile());
}

void MainComponent::saveWorldToFileAs()
{
    if (saveAsChooser != nullptr)
        return;

    juce::File defaultFile = worldSaveFile();
    saveAsChooser = std::make_unique<juce::FileChooser>("Save KlangKunst World As",
                                                         defaultFile,
                                                         "*.mat");

    constexpr int flags = juce::FileBrowserComponent::saveMode
                        | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting;

    juce::Component::SafePointer<MainComponent> safeThis(this);
    saveAsChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        juce::File target = chooser.getResult();
        if (target != juce::File())
        {
            if (! target.hasFileExtension("mat"))
                target = target.withFileExtension(".mat");

            safeThis->writeWorldToFile(target);
            safeThis->hasSession = true;
            safeThis->repaint();
        }

        safeThis->saveAsChooser.reset();
    });
}

void MainComponent::writeWorldToFile(const juce::File& targetFile)
{
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> columnRows;
    juce::Array<juce::var> toolRows;

    for (int y = 0; y < worldSize; ++y)
    {
        juce::Array<juce::var> colRow;
        juce::Array<juce::var> trow;
        for (int x = 0; x < worldSize; ++x)
        {
            colRow.add(static_cast<juce::int64>(columns[x][y]));
            juce::DynamicObject::Ptr t = new juce::DynamicObject();
            t->setProperty("type", static_cast<int>(tools[x][y].type));
            t->setProperty("rotation", tools[x][y].rotation);
            t->setProperty("state", tools[x][y].state);
            trow.add(juce::var(t.get()));
        }
        columnRows.add(colRow);
        toolRows.add(trow);
    }

    root->setProperty("columns", columnRows);
    root->setProperty("tools", toolRows);
    root->setProperty("worldSize", worldSize);
    root->setProperty("bpm", bpm);
    root->setProperty("keyRoot", keyRoot);
    root->setProperty("scale", static_cast<int>(scale));
    root->setProperty("beatStyle", beatStyle);
    root->setProperty("arrangementEnabled", arrangementEnabled);
    root->setProperty("arrangementSequenceIndex", arrangementSequenceIndex);
    root->setProperty("arrangementBarsInSection", arrangementBarsInSection);

    targetFile.replaceWithText(juce::JSON::toString(juce::var(root.get()), true));
}

void MainComponent::loadWorldFromFile()
{
    const auto saveFile = worldSaveFile();
    if (! saveFile.existsAsFile())
        return;

    const juce::var parsed = juce::JSON::parse(saveFile);
    if (! parsed.isObject())
        return;

    auto* obj = parsed.getDynamicObject();
    if (obj == nullptr)
        return;

    if (obj->hasProperty("worldSize"))
        worldSize = juce::jlimit(12, maxWorldSize, static_cast<int>(obj->getProperty("worldSize")));
    else
        worldSize = 16;

    resetBlankWorld();

    const auto columnsVar = obj->getProperty("columns");
    if (columnsVar.isArray())
    {
        const auto* rows = columnsVar.getArray();
        for (int y = 0; y < juce::jmin(worldSize, rows->size()); ++y)
        {
            const auto& row = rows->getReference(y);
            if (! row.isArray())
                continue;
            const auto* arr = row.getArray();
            for (int x = 0; x < juce::jmin(worldSize, arr->size()); ++x)
                columns[x][y] = static_cast<uint32_t>(static_cast<juce::int64>(arr->getReference(x)));
        }
    }
    else
    {
        const auto rowsVar = obj->getProperty("heights");
        if (rowsVar.isArray())
        {
            const auto* rows = rowsVar.getArray();
            for (int y = 0; y < juce::jmin(worldSize, rows->size()); ++y)
            {
                const auto& row = rows->getReference(y);
                if (! row.isArray())
                    continue;
                const auto* arr = row.getArray();
                for (int x = 0; x < juce::jmin(worldSize, arr->size()); ++x)
                {
                    const int h = juce::jlimit(0, maxHeight - 1, static_cast<int>(arr->getReference(x)));
                    for (int z = 1; z <= h; ++z)
                        setBlock(x, y, z, true);
                }
            }
        }
    }

    const auto toolRowsVar = obj->getProperty("tools");
    if (toolRowsVar.isArray())
    {
        const auto* rows = toolRowsVar.getArray();
        for (int y = 0; y < juce::jmin(worldSize, rows->size()); ++y)
        {
            const auto& row = rows->getReference(y);
            if (! row.isArray())
                continue;
            const auto* arr = row.getArray();
            for (int x = 0; x < juce::jmin(worldSize, arr->size()); ++x)
            {
                const auto& tv = arr->getReference(x);
                if (! tv.isObject())
                    continue;
                if (auto* to = tv.getDynamicObject())
                {
                    tools[x][y].type = static_cast<ToolType>(juce::jlimit(0, 6, static_cast<int>(to->getProperty("type"))));
                    tools[x][y].rotation = static_cast<int>(to->getProperty("rotation")) % 4;
                    tools[x][y].state = static_cast<int>(to->getProperty("state"));
                }
            }
        }
    }

    if (obj->hasProperty("bpm"))
        bpm = static_cast<double>(obj->getProperty("bpm"));
    if (obj->hasProperty("keyRoot"))
        keyRoot = juce::jlimit(0, 11, static_cast<int>(obj->getProperty("keyRoot")));
    if (obj->hasProperty("scale"))
        scale = static_cast<ScaleType>(juce::jlimit(0, 4, static_cast<int>(obj->getProperty("scale"))));
    if (obj->hasProperty("beatStyle"))
        beatStyle = juce::jlimit(0, 4, static_cast<int>(obj->getProperty("beatStyle")));
    if (obj->hasProperty("arrangementEnabled"))
        arrangementEnabled = static_cast<bool>(obj->getProperty("arrangementEnabled"));
    if (obj->hasProperty("arrangementSequenceIndex"))
        arrangementSequenceIndex = juce::jlimit(0, static_cast<int>(arrangementSections.size()) - 1,
                                                static_cast<int>(obj->getProperty("arrangementSequenceIndex")));
    if (obj->hasProperty("arrangementBarsInSection"))
        arrangementBarsInSection = juce::jmax(0, static_cast<int>(obj->getProperty("arrangementBarsInSection")));

    hasSession = true;
    invalidateAllCaches();
}

int MainComponent::activeArrangementSection() const
{
    return arrangementSections[static_cast<size_t>(juce::jlimit(0, static_cast<int>(arrangementSections.size()) - 1, arrangementSequenceIndex))];
}

const char* MainComponent::sectionShortName(int sectionId) const
{
    switch (sectionId)
    {
        case 0: return "IN";
        case 1: return "V";
        case 2: return "PRE";
        case 3: return "CH";
        case 4: return "M8";
        default: break;
    }
    return "V";
}

const char* MainComponent::beatStyleName(int style) const
{
    switch (style)
    {
        case 0: return "Off";
        case 1: return "909 Rez Straight";
        case 2: return "909 Tight Pulse";
        case 3: return "909 Forward Step";
        case 4: return "909 Rail Line";
        default: break;
    }
    return "Off";
}

void MainComponent::triggerBeatLayer(double beatTime)
{
    if (beatStyle <= 0)
        return;

    const int beatsPerBar = beatsPerBarForWorldSize(worldSize);
    const int stepsPerBar = beatsPerBar * 4; // 16th grid
    const auto beatInBar = std::fmod(beatTime, static_cast<double>(beatsPerBar));
    const auto wrapped = (beatInBar < 0.0 ? beatInBar + static_cast<double>(beatsPerBar) : beatInBar);
    const auto step = static_cast<int>(std::floor(wrapped * 4.0 + 1.0e-6)) % stepsPerBar;
    const auto beatAtStep = [stepsPerBar, beatsPerBar](int beat) { return (beat * stepsPerBar) / beatsPerBar; };

    auto hit = [this] (int midiNote, float velocity)
    {
        // Keep beats more supportive/subtle under melodic material.
        triggerBeatMidi(midiNote, juce::jlimit(0.0f, 1.0f, velocity * 0.90f));
    };

    switch (beatStyle)
    {
        case 1:
            if ((step % 4) == 0) hit(120, 0.82f);
            if (step == beatAtStep(1) || (beatsPerBar >= 4 && step == beatAtStep(3))) hit(121, 0.60f);
            if ((step % 2) == 1) hit(122, 0.18f + ((step % 4) == 3 ? 0.05f : 0.0f));
            break;

        case 2:
            if (step == 0 || step == beatAtStep(2) || step == beatAtStep(beatsPerBar - 1)) hit(120, 0.78f);
            if (step == beatAtStep(1) || (beatsPerBar >= 4 && step == beatAtStep(3))) hit(121, 0.58f);
            if ((step % 2) == 1) hit(122, 0.17f);
            if (step == (stepsPerBar - 1)) hit(123, 0.18f);
            break;

        case 3:
            if (step == 0 || step == (stepsPerBar / 2) || step == (stepsPerBar - 2)) hit(120, 0.76f);
            if (step == beatAtStep(1) || (beatsPerBar >= 4 && step == beatAtStep(3))) hit(121, 0.56f);
            if ((step % 2) == 1) hit(122, 0.16f + ((step == (stepsPerBar - 5) || step == (stepsPerBar - 1)) ? 0.05f : 0.0f));
            break;

        case 4:
            if ((step % 4) == 0) hit(120, 0.72f);
            if (step == beatAtStep(1) || (beatsPerBar >= 4 && step == beatAtStep(3))) hit(121, 0.52f);
            if ((step % 2) == 1) hit(122, 0.15f);
            if (step == beatAtStep(beatsPerBar - 2) || step == (stepsPerBar - 1)) hit(123, 0.16f);
            break;

        default:
            break;
    }
}

void MainComponent::advanceArrangementBar()
{
    if (! arrangementEnabled)
        return;

    const int section = activeArrangementSection();
    const int barsForSection = arrangementBarsPerSection[static_cast<size_t>(juce::jlimit(0, static_cast<int>(arrangementBarsPerSection.size()) - 1, section))];
    ++arrangementBarsInSection;
    if (arrangementBarsInSection < barsForSection)
        return;

    arrangementBarsInSection = 0;
    arrangementSequenceIndex = (arrangementSequenceIndex + 1) % static_cast<int>(arrangementSections.size());
}
