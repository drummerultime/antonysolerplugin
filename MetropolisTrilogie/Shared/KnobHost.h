#pragma once

namespace alea::gui
{
// Ce que le potard attend du processeur (MIDI learn, historique)
struct KnobHost
{
    virtual ~KnobHost() = default;
    virtual void pushUndo() = 0;
    virtual int  ccForParam (int paramIdx) const = 0;
    virtual void clearMidiMapping (int paramIdx) = 0;
    virtual void startLearn (int paramIdx) = 0;
    virtual int  learningParam() const = 0;
};
} // namespace alea::gui
