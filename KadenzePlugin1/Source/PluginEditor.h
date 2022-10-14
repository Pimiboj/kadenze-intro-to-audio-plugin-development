/*
  ==============================================================================

    This file contains the basic framework code for a JUCE plugin editor.

  ==============================================================================
*/

#pragma once

#include <JuceHeader.h>
#include "PluginProcessor.h"

//==============================================================================
/**
*/
class KadenzePlugin1AudioProcessorEditor  : public juce::AudioProcessorEditor
{
public:
    KadenzePlugin1AudioProcessorEditor (KadenzePlugin1AudioProcessor&);
    ~KadenzePlugin1AudioProcessorEditor() override;

    //==============================================================================
    void paint (juce::Graphics&) override;
    void resized() override;

private:

    juce::Slider mGainControlSlider;
    // This reference is provided as a quick way for your editor to
    // access the processor object that created it.
    KadenzePlugin1AudioProcessor& audioProcessor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KadenzePlugin1AudioProcessorEditor)
};
