#pragma once

// Converts the styled text in notification bodies to Pango markup. It follows Qt's StyledText rules so
// bodies render as they always have: a small HTML subset, entities, whitespace collapsed outside <pre>,
// unknown tags dropped (their text kept). The output is always well-formed: runs are re-emitted as flat
// <span>s around escaped text, never by passing the input's tags through.

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace kusanagi {

  struct StyledParagraph {
    std::string markup;   // Pango markup, empty for an empty line
    bool scaled = false;  // some runs have another size (<font size>, <hN>)
  };

  // One paragraph per line break (<br>, <p>, <li>, ...; a trailing break gives no empty last paragraph).
  // The card's line limit counts over the whole text, while Pango's line budget is per paragraph.
  struct StyledMarkup {
    std::vector<StyledParagraph> paragraphs;
    bool colored = false; // some runs carry their own colour (<font color>, links)
  };

  // `baseAlpha` (0..1) is the alpha for runs without a colour of their own when `colored`; the label is
  // then drawn at full alpha so coloured runs keep theirs. `lineHeight(scale, bold)`, when given, sets every
  // run's Pango line_height (absolute, Pango units) so lines keep the card's pitch per run size (scale 1
  // is the label's size).
  [[nodiscard]] StyledMarkup styledTextToPango(
      std::string_view styled, float baseAlpha = 1.0F, const std::function<long(float, bool)>& lineHeight = {}
  );

  // The first line of the body with tags stripped and entities decoded, for the minimal pill.
  [[nodiscard]] std::string styledFirstLine(std::string_view styled);

} // namespace kusanagi
