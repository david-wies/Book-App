#pragma once

// Design tokens for BookHub.
// All values are defined in the GUI design spec Section 8.1.
// Use these constants everywhere rather than hard-coding colour/spacing values
// so future theme changes require edits in one place only.

#include <QString>

namespace bookhub::gui {

// ---------------------------------------------------------------------------
// Colours
// ---------------------------------------------------------------------------

inline constexpr const char* ColorAccent        = "#2563EB"; // primary buttons, active tab
inline constexpr const char* ColorAccentHover   = "#1D4ED8"; // button hover
inline constexpr const char* ColorSuccess       = "#16A34A"; // downloaded, checkmarks
inline constexpr const char* ColorWarning       = "#D97706"; // downloading / converting spinner
inline constexpr const char* ColorError         = "#DC2626"; // error states
inline constexpr const char* ColorTextPrimary   = "#111827"; // titles, primary labels
inline constexpr const char* ColorTextMuted     = "#6B7280"; // author names, metadata
inline constexpr const char* ColorSurface       = "#FFFFFF"; // card backgrounds
inline constexpr const char* ColorBackground    = "#F9FAFB"; // screen background
inline constexpr const char* ColorBorder        = "#E5E7EB"; // card borders, separators
inline constexpr const char* ColorNavBg         = "#1E293B"; // navigation bar background
inline constexpr const char* ColorNavText       = "#F1F5F9"; // navigation button text
inline constexpr const char* ColorNavActive     = "#2563EB"; // active tab bottom border

// Badge pill colours (Section 8.2)
inline constexpr const char* ColorLangBadgeBg   = "#EFF6FF";
inline constexpr const char* ColorLangBadgeText = "#1D4ED8";
inline constexpr const char* ColorLangBadgeBorder= "#BFDBFE";

inline constexpr const char* ColorSrcBadgeBg    = "#F0FDF4";
inline constexpr const char* ColorSrcBadgeText  = "#15803D";
inline constexpr const char* ColorSrcBadgeBorder= "#BBF7D0";

inline constexpr const char* ColorTagBg         = "#F3F4F6";
inline constexpr const char* ColorTagText       = "#374151";

// ---------------------------------------------------------------------------
// Spacing (pixels)
// ---------------------------------------------------------------------------

inline constexpr int SpacingXS = 4;
inline constexpr int SpacingSM = 8;
inline constexpr int SpacingMD = 16;
inline constexpr int SpacingLG = 24;
inline constexpr int SpacingXL = 32;

// ---------------------------------------------------------------------------
// Typography (point sizes)
// ---------------------------------------------------------------------------

inline constexpr int FontSizeTitle    = 18;
inline constexpr int FontSizeSubtitle = 14;
inline constexpr int FontSizeBody     = 12;
inline constexpr int FontSizeMeta     = 11;
inline constexpr int FontSizeBadge    = 10;

// ---------------------------------------------------------------------------
// Border radii (pixels)
// ---------------------------------------------------------------------------

inline constexpr int RadiusSM   = 4;
inline constexpr int RadiusMD   = 8;
inline constexpr int RadiusLG   = 12;
inline constexpr int RadiusPill = 999;

// ---------------------------------------------------------------------------
// Component dimensions
// ---------------------------------------------------------------------------

inline constexpr int NavBarHeight      = 48;
inline constexpr int StatusBarHeight   = 24;
inline constexpr int LibraryToolbarH   = 44;
inline constexpr int BookCardRowHeight = 96;    // list mode
inline constexpr int BookCardGridW     = 200;
inline constexpr int BookCardGridH     = 260;
inline constexpr int CoverThumbW       = 64;
inline constexpr int CoverThumbH       = 88;

// Minimum and default window dimensions
inline constexpr int WindowMinWidth    = 900;
inline constexpr int WindowMinHeight   = 650;
inline constexpr int WindowDefWidth    = 1200;
inline constexpr int WindowDefHeight   = 800;

} // namespace bookhub::gui
