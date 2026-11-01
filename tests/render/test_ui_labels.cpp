// SPDX-License-Identifier: GPL-3.0-or-later
//
// The file/rank coordinate labels, and where they are allowed to appear (M17.21 label
// revision).
//
// They are text projected through the camera, and they read badly in any 3-D view: a
// letter skews, overlaps its neighbour, or goes nearly edge-on. The decision to show them
// is one predicate, so it is pinned here rather than left implicit inside `buildGameHud`.
#include <catch2/catch_test_macros.hpp>

#ifdef CB_HAVE_IMGUI

#include <string>

#include "app/session.hpp"
#include "render/ui.hpp"
#include "support/variants.hpp"

using namespace cb;
using namespace cb::render;

TEST_CASE("the public menu omits unfinished entries and renumbers the remaining rows",
          "[render][dev-overlay]") {
  for (bool dev : {false, true}) {
    const auto entries = mainMenuEntries(dev);
    const std::vector<std::string> expected =
        dev ? std::vector<std::string>{"New game", "Watch a game", "Watch trailer",
                                       "Continue", "Multiplayer",  "Tutorial",
                                       "Editor",   "Settings",     "Quit"}
            : std::vector<std::string>{"New game", "Watch a game", "Watch trailer",
                                       "Tutorial", "Editor",       "Settings",
                                       "Quit"};
    REQUIRE(entries.size() == expected.size());
    for (std::size_t i = 0; i < entries.size(); ++i) {
      CHECK(entries[i].label == expected[i]);
      CHECK(entries[i].index == static_cast<int>(i + 1));
      CHECK(entries[i].enabled ==
            (expected[i] != "Continue" && expected[i] != "Multiplayer"));
    }
  }
}

TEST_CASE("coordinate labels are drawn only in the flat 2-D view", "[render]") {
  app::Settings settings;
  auto session = app::Session::create(test::loadVariant("standard"));
  REQUIRE(session.has_value());
  app::Session& s = **session;

  // The one place they belong: flat, orthographic, two dimensions.
  settings.showCoordinates = true;
  s.setFlatView(true);
  CHECK(coordinateLabelsShown(settings, s));

  // Off the flat 2-D projection - the ordinary solid board and the shape view alike -
  // the camera-projected text is dropped.
  s.setFlatView(false);
  CHECK_FALSE(coordinateLabelsShown(settings, s));

  // The player's toggle still owns them.
  s.setFlatView(true);
  settings.showCoordinates = false;
  CHECK_FALSE(coordinateLabelsShown(settings, s));

  // And they are 2-D only, however flat the view.
  settings.showCoordinates = true;
  auto cube = app::Session::create(test::loadVariant("cube5"));
  REQUIRE(cube.has_value());
  (**cube).setFlatView(true);
  CHECK_FALSE(coordinateLabelsShown(settings, **cube));
}

TEST_CASE("the 2D/3D button is re-purposed on the shape view", "[render]") {
  // Off the shape view it is the original flat/solid board toggle, with its original
  // enabled state - exactly as before M17.25. On a shape that actually exists the old
  // flat meaning is dead (a shape has no 2D board), so the button becomes live again and
  // names the piece style instead. A board with `geometryView` but no surface must not
  // move.
  app::Settings settings;

  settings.geometryView = false;
  settings.flatView = true;
  {
    const ViewModeButton b = viewModeButton(settings, true, false);
    CHECK(std::string(b.label) == "2D");
    CHECK(b.enabled);
    CHECK_FALSE(b.pieceStyle);
  }
  settings.flatView = false;
  {
    const ViewModeButton b = viewModeButton(settings, false, false);
    CHECK(std::string(b.label) == "3D");
    CHECK(b.enabled);
    CHECK_FALSE(b.pieceStyle);
  }

  // Reported directly: `geometryView` left on from a *different*, glued variant (SHAPE
  // has no way to turn itself off on a plain switch to e.g. standard or cube5) must not
  // disable the board toggle there - there is no surface for it to have any effect on,
  // so disabling it serves no purpose and only breaks the ordinary flat/solid toggle on
  // every variant without a shape. Live, and still the plain board meaning, not the
  // piece-style one - `surfaceAvailable` is false, so the first branch cannot fire.
  settings.geometryView = true;
  {
    const ViewModeButton b = viewModeButton(settings, false, false);
    CHECK(std::string(b.label) == "3D");
    CHECK(b.enabled);
    CHECK_FALSE(b.pieceStyle);
  }

  // The real shape view: live, and naming the piece style.
  settings.shapePieceCylinders = false;
  {
    const ViewModeButton b = viewModeButton(settings, false, true);
    CHECK(std::string(b.label) == "3D");
    CHECK(b.enabled);  // live on the shape view, where it used to be greyed out
    CHECK(b.pieceStyle);
  }
  settings.shapePieceCylinders = true;
  {
    const ViewModeButton b = viewModeButton(settings, false, true);
    CHECK(std::string(b.label) == "2D");
    CHECK(b.enabled);
    CHECK(b.pieceStyle);
  }
  // Flat wins over the shape, so there the board toggle is in charge - and live, because
  // it is the only way back: SHAPE is greyed in 2D, so a greyed toggle too was a dead
  // end.
  {
    const ViewModeButton b = viewModeButton(settings, true, true);
    CHECK(std::string(b.label) == "2D");
    CHECK(b.enabled);
    CHECK_FALSE(b.pieceStyle);
  }
}

#endif  // CB_HAVE_IMGUI
