#include "GameComponent.h"

class MainWindow final : public juce::DocumentWindow
{
public:
    MainWindow() : juce::DocumentWindow ("KlangKunst Galaxy",
                                         juce::Colours::black,
                                         juce::DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setResizable (false, false);
        setContentOwned (new GameComponent(), true);
        setFullScreen (true);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class KlangKunstGalaxyApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "KlangKunstGalaxy"; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override          { return true; }

    void initialise (const juce::String&) override
    {
        mainWindow = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        mainWindow.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (KlangKunstGalaxyApplication)
