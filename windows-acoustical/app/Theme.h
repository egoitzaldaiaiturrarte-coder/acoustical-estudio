// Theme.h — paleta Deep Ocean, el mismo lenguaje visual que la app móvil.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace theme {
using juce::Colour;

inline constexpr Colour background   { 0xff061420 };  // fondo profundo
inline constexpr Colour surface      { 0xff0b1b2b };  // tarjetas
inline constexpr Colour surfaceHi    { 0xff12293c };  // tarjetas elevadas
inline constexpr Colour primary      { 0xff22d3ee };  // cian
inline constexpr Colour primaryDark  { 0xff0e7490 };
inline constexpr Colour textPrimary  { 0xffe2f2fa };
inline constexpr Colour textDim      { 0xff7ba2b8 };
inline constexpr Colour eq1Cyan      { 0xff22d3ee };
inline constexpr Colour eq2Amber     { 0xfffbbf24 };
inline constexpr Colour eq3Magenta   { 0xffe879f9 };
inline constexpr Colour positive     { 0xff34d399 };
inline constexpr Colour danger       { 0xfff87171 };

inline void apply(juce::Component& root) {
    root.setColour(juce::ResizableWindow::backgroundColourId, background);
}
} // namespace theme
