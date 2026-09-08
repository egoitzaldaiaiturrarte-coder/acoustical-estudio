// Theme.h — paleta Deep Ocean, el mismo lenguaje visual que la app móvil.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme {
using juce::Colour;

inline const Colour background   { 0xff061420 };  // fondo profundo
inline const Colour surface      { 0xff0b1b2b };  // tarjetas
inline const Colour surfaceHi    { 0xff12293c };  // tarjetas elevadas
inline const Colour primary      { 0xff22d3ee };  // cian
inline const Colour primaryDark  { 0xff0e7490 };
inline const Colour textPrimary  { 0xffe2f2fa };
inline const Colour textDim      { 0xff7ba2b8 };
inline const Colour eq1Cyan      { 0xff22d3ee };
inline const Colour eq2Amber     { 0xfffbbf24 };
inline const Colour eq3Magenta   { 0xffe879f9 };
inline const Colour positive     { 0xff34d399 };
inline const Colour danger       { 0xfff87171 };

inline void apply(juce::Component& root) {
    root.setColour(juce::ResizableWindow::backgroundColourId, background);
}
} // namespace theme
