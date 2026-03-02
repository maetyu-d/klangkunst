#pragma once

#include <JuceHeader.h>

class MainComponent final : public juce::AudioAppComponent,
                            public juce::KeyListener,
                            public juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;

    bool keyPressed(const juce::KeyPress& key, juce::Component*) override;

    void timerCallback() override;

private:
    static constexpr int worldSize = 16;
    static constexpr int maxHeight = 25;

public:
    enum class Mode { title, build, performance };
    enum class MenuAction { resume, save, load, demo, blank };
    enum class ToolType { none, redirect, speed, ratchet, key, scale, section };
    enum class ScaleType { chromatic, major, minor, dorian, pentatonic };
    enum class PlayMode { melodic, chord, arpeggio };
    enum class SynthEngine { digitalV4, fmGlass, velvetNoise };
private:

    struct PlayerCursor
    {
        int x = 0;
        int y = 0;
        int z = 1;
        juce::Colour colour;
    };

    struct Tool
    {
        ToolType type = ToolType::none;
        int rotation = 0;
        int state = 0;
    };

    struct Snake
    {
        juce::Point<int> cell { 0, 0 };
        juce::Point<int> dir { 1, 0 };
        int speedState = 1;
        int ratchet = 1;
        juce::Colour colour;
        std::vector<juce::Point<float>> trail;
    };

    struct Pulse
    {
        juce::Point<float> p;
        float age = 0.0f;
        float maxAge = 0.45f;
        juce::Colour c;
        float size = 1.0f;
    };

    struct Spark
    {
        juce::Point<float> p;
        juce::Point<float> v;
        float age = 0.0f;
        float maxAge = 0.35f;
        float size = 3.0f;
        juce::Colour c;
    };

    struct PendingNoteOff
    {
        int note = 60;
        float secondsRemaining = 0.2f;
    };

    class WaveVoice final : public juce::SynthesiserVoice
    {
    public:
        explicit WaveVoice(SynthEngine& engineRef) : engine(engineRef) {}

        bool canPlaySound(juce::SynthesiserSound* s) override;
        void startNote(int midiNoteNumber, float velocity, juce::SynthesiserSound*, int) override;
        void stopNote(float velocity, bool allowTailOff) override;
        void pitchWheelMoved(int) override {}
        void controllerMoved(int, int) override {}
        void renderNextBlock(juce::AudioBuffer<float>& outputBuffer, int startSample, int numSamples) override;

    private:
        SynthEngine& engine;
        juce::ADSR adsr;
        juce::ADSR::Parameters adsrParams;
        double currentSampleRate = 44100.0;
        float level = 0.0f;
        double angleDelta = 0.0;
        double currentAngle = 0.0;
        double modAngle = 0.0;
        double modDelta = 0.0;
        double subAngle = 0.0;
        double subDelta = 0.0;
        float noteAgeSeconds = 0.0f;
        uint32_t noiseSeed = 0u;
        float sampleHoldValue = 0.0f;
        int sampleHoldCounter = 0;
        int sampleHoldPeriod = 1;
        float lpState = 0.0f;
        float hpState = 0.0f;
        bool percussionMode = false;
        int percussionType = 0; // 0 body, 1 snap, 2 hat, 3 glitch
        float noiseLP = 0.0f;
        float noiseHP = 0.0f;
        float lastNoise = 0.0f;
    };

    class WaveSound final : public juce::SynthesiserSound
    {
    public:
        bool appliesToNote(int) override { return true; }
        bool appliesToChannel(int) override { return true; }
    };

    class Miverb
    {
    public:
        void prepare(double sampleRate);
        void reset();
        void setMix(float v) { mix = juce::jlimit(0.0f, 1.0f, v); }
        void setSize(float v) { size = juce::jlimit(0.0f, 1.0f, v); }
        void setDecay(float v) { decay = juce::jlimit(0.0f, 1.2f, v); }
        void setDiffusion(float v) { diffusion = juce::jlimit(0.0f, 0.98f, v); }
        void setTone(float lp, float hp)
        {
            lowpass = juce::jlimit(0.02f, 1.0f, lp);
            highpass = juce::jlimit(0.0f, 0.98f, hp);
        }
        void process(juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    private:
        struct DelayLine
        {
            std::vector<float> data;
            int write = 0;

            void resize(int n)
            {
                data.assign(static_cast<size_t>(juce::jmax(2, n)), 0.0f);
                write = 0;
            }

            void clear()
            {
                std::fill(data.begin(), data.end(), 0.0f);
                write = 0;
            }
        };

        float readLinear(const DelayLine& d, float delaySamples) const;
        float allpassProcess(DelayLine& d, float input, float delaySamples, float g);
        float delayProcess(DelayLine& d, float input, float delaySamples);

        double sr = 44100.0;
        float mix = 0.24f;
        float size = 0.64f;
        float decay = 0.82f;
        float diffusion = 0.72f;
        float lowpass = 0.74f;
        float highpass = 0.06f;
        float modRate = 0.19f;
        float modAmount = 15.0f;
        float phase = 0.0f;
        float lpL = 0.0f;
        float lpR = 0.0f;
        float hpL = 0.0f;
        float hpR = 0.0f;

        DelayLine ap1, ap2, ap3, ap4;
        DelayLine tankLAp, tankRAp, tankLDelay, tankRDelay;
    };

    void resetBlankWorld();
    void loadDemoWorld();

    void invalidateAllCaches();
    void updateTitleCache();
    void updateBuildCache();
    void updatePerformanceCache();

    void paintTitle(juce::Graphics& g);
    void paintBuild(juce::Graphics& g);
    void paintPerformance(juce::Graphics& g);

    void drawTitleLiveOverlays(juce::Graphics& g);
    void drawBuildOverlays(juce::Graphics& g);
    void drawPerformanceOverlays(juce::Graphics& g);

    bool handleTitleKeys(const juce::KeyPress& key);
    bool handleBuildKeys(const juce::KeyPress& key);
    bool handlePerformanceKeys(const juce::KeyPress& key);

    void startPerformanceMode();
    void enterBuildMode();

    void performMenuAction(MenuAction action);
    std::vector<MenuAction> getTitleMenu() const;
    bool isTitleActionEnabled(MenuAction action) const;

    juce::Point<float> isoToScreen(int x, int y, int z, float tileW, float tileH, juce::Point<float> origin, int rot) const;
    juce::Point<float> getBuildOrigin(float tileW, float tileH) const;
    juce::Point<int> rotateCell(int x, int y, int rot) const;

    juce::Colour pitchClassColour(int semitone) const;
    int quantizeMidiToScale(int midi) const;
    std::vector<int> currentScaleSteps() const;
    int quantizeMidiToCurrentScaleStrict(int midi) const;
    bool quantizeWorldToCurrentScale();
    int zToMidi(int z) const;
    bool hasBlock(int x, int y, int z) const;
    void setBlock(int x, int y, int z, bool enabled);
    int topBlockZ(int x, int y) const;

    void triggerNoteForCell(int x, int y, float velocity, float noteLengthSeconds, int ratchet = 1, int gateSection = -1);
    void triggerMidi(int midiNote, float velocity, float noteLengthSeconds);
    void addJuiceAtCell(int x, int y, juce::Colour c, float strength);

    void advanceTransport(double deltaSeconds);
    void advancePerformanceStep();
    void applyToolToSnake(Snake& snake);
    int activeArrangementSection() const;
    const char* sectionShortName(int sectionId) const;
    void advanceArrangementBar();
    const char* beatStyleName(int style) const;
    void triggerBeatLayer(double beatTime);

    void saveWorldToFile();
    void saveWorldToFileAs();
    void loadWorldFromFile();
    void writeWorldToFile(const juce::File& targetFile);

    Mode mode = Mode::title;
    bool hasSession = false;
    bool hasVisitedBuildMode = false;

    std::array<std::array<uint32_t, worldSize>, worldSize> columns {};
    std::array<std::array<Tool, worldSize>, worldSize> tools;

    PlayerCursor p1 { 2, 2, 0, juce::Colours::cyan };
    PlayerCursor p2 { worldSize - 3, worldSize - 3, 0, juce::Colours::orange };
    int viewRotation = 0;

    Snake snakeA;
    Snake snakeB;
    std::vector<Pulse> pulses;
    std::vector<Spark> sparks;

    int selectedMenu = 0;
    int selectedToolIndex = 1;

    bool quantizeToScale = true;
    int keyRoot = 0;
    ScaleType scale = ScaleType::minor;
    PlayMode playMode = PlayMode::melodic;
    SynthEngine synthEngine = SynthEngine::digitalV4;

    double bpm = 118.0;
    double beatPhase = 0.0;
    int beatCounter = 0;
    int barCounter = 0;
    bool barPulse = false;
    bool arrangementEnabled = true;
    int arrangementSequenceIndex = 0;
    int arrangementBarsInSection = 0;
    int beatStyle = 0; // 0=off, 1..4 patterns
    double nextBeatLayerBeat = 0.0;
    double performanceMoveAccumulator = 0.0;
    int performanceStepCounter = 0;
    int countdownBeats = 0;

    juce::Image titleCache;
    juce::Image buildCache;
    juce::Image performanceCache;
    bool titleCacheDirty = true;
    bool buildCacheDirty = true;
    bool performanceCacheDirty = true;

    float titlePhase = 0.0f;
    float globalJuice = 0.0f;
    float beatFlash = 0.0f;
    float screenShake = 0.0f;

    juce::CriticalSection synthLock;
    juce::Synthesiser synth;
    std::unique_ptr<juce::FileChooser> saveAsChooser;
    Miverb miverb;
    bool miverbEnabled = true;
    float miverbMix = 0.24f;
    std::vector<PendingNoteOff> pendingNoteOffs;
    double currentSr = 44100.0;
    double elapsedSinceTick = 0.0;
    double lastTimerMs = 0.0;
    juce::Random rng;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
