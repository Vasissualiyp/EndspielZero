// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <functional>
#include <vector>

#include "render/image_io.hpp"
#include "render/offscreen_target.hpp"
#include "render/piece_mesh.hpp"
#include "space/coord.hpp"
#include "view/camera.hpp"
#include "view/layout.hpp"
#include "view/move_anim.hpp"
#include "view/seams.hpp"
#include "view/snapshot.hpp"
#include "view/theme.hpp"

namespace cb::render {

/// The colour parity of a square: the sum of its coordinates on `drawnAxes` (even =
/// dark). With `swapLayers`, every axis beyond the first two that is not already one of
/// the drawn axes also counts, so the second, fourth ... layer of a 3-D board is the
/// colour opposite to the layer below it instead of a copy of it.
[[nodiscard]] inline int squareParity(const Coord& c, const std::uint8_t* drawnAxes,
                                      std::size_t drawnCount, bool swapLayers) noexcept {
  int parity = 0;
  for (std::size_t i = 0; i < drawnCount; ++i) parity += c.c[drawnAxes[i]];
  if (swapLayers) {
    for (std::uint8_t a = 2; a < c.n; ++a) {
      bool drawn = false;
      for (std::size_t i = 0; i < drawnCount; ++i) drawn = drawn || drawnAxes[i] == a;
      if (!drawn) parity += c.c[a];
    }
  }
  return parity;
}

/// One instanced box or piece.
struct Instance {
  float center[3]{};
  float scale[3]{};
  float color[4]{};
  /// Seam colour, alpha 0 when this cell is not on a glued face.
  float edge[4]{};
  /// Which sides are glued: bit 0 = -x, 1 = +x, 2 = -y, 3 = +y.
  float edgeMask{0};
  /// Rotation about the mesh's own Z, in radians. Lets one mesh - the rounded corner
  /// of a timeline connector - be placed in any of the four quadrants without four
  /// copies of the geometry.
  float roll{0};
  /// Full orientation as a quaternion (x,y,z,w), applied after `roll`. Identity on the
  /// flat board; on the geometry view it turns a cell's local +Z onto the surface normal,
  /// so a tile or a piece stands on the warped board with the ordinary camera and depth
  /// buffer (M17).
  float quat[4]{0.0f, 0.0f, 0.0f, 1.0f};
  /// Shader material: integer part selects matte (0), metal (1), plank wood (2),
  /// or turned wood (6); ids 3-5 are reserved. Fractional part varies wood grain.
  float material{0};
};
static_assert(sizeof(Instance) == 84);

/// The sub-rectangle of the target the board is drawn into, in pixels. Zero width means
/// the whole target. At namespace scope because a nested class's default member
/// initializers cannot be used in a default argument inside the enclosing class.
struct BoardRect {
  float x{0}, y{0}, width{0}, height{0};
  [[nodiscard]] bool valid() const noexcept { return width > 0 && height > 0; }
};

/// What the board shows. Driven by the player's settings; the renderer holds no
/// opinions of its own about what is worth marking.
struct BoardOptions {
  bool showLegalMoves{true};
  bool showLastMove{true};
  /// Flip square colours on every second layer of a board with 3+ axes
  /// (`Settings::swapLayerColors`).
  bool swapLayerColors{false};
  bool showCheck{true};
  bool showSeams{true};
  /// Nothing closer to the camera than this many cells is drawn (`Settings::nearClip`).
  float nearClip{0.1f};
  /// 1 keeps the height a piece's value implies; 0 makes every piece the same height.
  float pieceHeightScale{1.0f};
  /// Draw the board flat, looking straight down, with pieces as tokens rather than as
  /// models. Fewer triangles, no orbiting, and - the point of it - readable on a
  /// machine that cannot comfortably draw the three-dimensional scene.
  bool flat{false};
  /// Draw the board as the surface its geometry describes: cells become tiles on the
  /// warped surface and pieces ride its normals, all through the ordinary camera (M17).
  /// Only meaningful for a glued 2-D variant; `buildInstances` ignores it otherwise.
  bool surface{false};
  /// Where the board sits on that surface, in cells: slide it one along the files and a1
  /// is where b1 was. A pose, not a shape - the cells, the moves and the position are
  /// untouched at any value (M17).
  float surfaceSlideU{0.0f};
  float surfaceSlideV{0.0f};
  /// The `hyper4` tesseract's own two live fourth-dimension rotation angles, in radians
  /// (M17.12 part B): the middle-drag's 4D turn, where a glued board has a slide. U is
  /// the file/aeon plane the library's spin used, so 0.9 is the pre-change picture; V is
  /// a second rank/aeon plane, 0 at rest. Ignored by every other shape.
  float surfaceHyperSpinU{0.9f};
  float surfaceHyperSpinV{0.0f};
  /// How far the surface has been turned through itself, 0 to 1 - a torus pulled inside
  /// out through its own hole, a cylinder rolled back over itself.
  float surfaceEvert{0.0f};
  /// How many half-turns the Klein bottle's ring twists over one lap. A diagnostic knob;
  /// 1 is the shipped surface.
  float surfaceTwist{1.0f};
  /// The ring or loop radius as a multiple of the shipped one, the cross-section
  /// unchanged: opens a tight torus/Klein bottle, or lengthens the Moebius loop.
  float surfaceOpenness{1.0f};
  /// The cross-section size as a multiple of the shipped one - the tube's thickness -
  /// leaving the ring radius alone.
  float surfaceThickness{1.0f};
  /// For the Klein bottle: how many board squares one rank end is rotated relative to the
  /// other before the collapse into the figure-eight (one square = 360/nx degrees),
  /// ramped along the rank so the square lines spiral.
  float surfaceCollapseSquares{0.0f};
  /// How far the surface has formed, 0 to 1 (M18.6): 0 is the flat board the SHAPE
  /// toggle starts from, 1 is the shipped shape. Default 1 so the flag is a no-op unless
  /// the front end is mid-morph.
  float surfaceFormed{1.0f};
  /// For a D >= 3 nested-shell shape: how far the levels have spread into their own
  /// radii, 0 to 1 - 0 is every level on one ring (the shape a 2-D torus already draws),
  /// 1 is the shipped nested shells (M13.9). Default 1, a no-op unless the front end is
  /// mid-morph, the same convention `surfaceFormed` uses.
  float surfaceShellSpread{1.0f};
  /// The board mesh's alpha, 1 opaque down to a translucent ghost so the far side of the
  /// shape and the pieces on it show through (M17.10). The pieces stay opaque.
  float surfaceGhost{1.0f};
  /// Draw the shape view's pieces as icon-faced pucks rather than the sculptural meshes
  /// (M17.25), so a seat the tangent-field walk has turned about-face still reads as the
  /// piece it is. Only meaningful for the surface path; `buildInstances` ignores it for
  /// the flat board and the ordinary solid board.
  bool shapePieceCylinders{false};
};

/// Instances grouped by the shape that draws them - one draw call per shape.
struct InstanceSet {
  std::vector<Instance> instances;
  struct Batch {
    std::uint32_t first{0};
    std::uint32_t count{0};
  };
  std::array<Batch, kShapeSlots> batches{};
  /// M17.26: how many of `batches[s]` are token discs rather than icon decals. The shape
  /// view emits the disc first and the icon second for each piece, both in this one
  /// batch; the first `discCounts[s]` draw from `discRanges[s]` and the rest from
  /// `cylinderRanges[s]`. Zero on every board that is not the shape view's puck board, so
  /// the ordinary per-shape draw is unchanged.
  std::array<std::uint32_t, kShapeSlots> discCounts{};

  /// The geometry view's board, as one mesh (M17). A square on a curved surface is a
  /// patch of that surface, not a slab turned to face it, so it cannot be an instance of
  /// anything: it is built fresh each frame with its colour in its vertices. Empty
  /// everywhere else, which is every screen but one.
  std::vector<MeshVertex> surfaceVertices;
  std::vector<std::uint32_t> surfaceIndices;
  /// Which instance carries the transform for that mesh - an identity one, because the
  /// vertices are already in world space. The pipeline needs *an* instance bound.
  std::uint32_t surfaceInstance{0};

  /// The capture flourish (M18.1): the captured piece shrinking out and the blood flash
  /// at the square it stood on. Kept apart from `instances` because these must be drawn
  /// with alpha blending - their alpha *is* the fade - in one pass after the opaque board
  /// rather than in the per-shape batches, which write depth and do not blend.
  std::vector<Instance> flourish;
  std::array<Batch, kShapeSlots> flourishBatches{};
  /// M17.26: the flourish's token discs, ahead of its icon decals in the same batch -
  /// `flourishBatches[s]`'s first `flourishDiscCounts[s]` draw from `discRanges[s]`.
  std::array<std::uint32_t, kShapeSlots>
      flourishDiscCounts{};

  /// The contact shadows (M18.2): one soft, dark decal under each occupied cell, flat or
  /// on a shape. Kept apart from `instances` for the same reason as the flourish - their
  /// softness *is* their alpha, so they cannot ride the opaque per-shape batches. Drawn
  /// in the same blended pass, before the flourish.
  std::vector<Instance> shadows;
  std::array<Batch, kShapeSlots> shadowBatches{};

  /// The `hyper4` wireframe scaffold (M17.12 part D): the tesseract's own 16-corner,
  /// 32-edge skeleton, drawn as thin bars so the eye has a small, stable frame to read
  /// the fourth-dimension rotation against instead of 256 independently moving tiles.
  /// Blended like the shadows, and for the same reason - a modest alpha is what keeps it
  /// a wire rather than a strut - so it cannot ride the opaque per-shape batches. Always
  /// fully drawn (depth-tested against the real board geometry); whether its far-side
  /// edges should dim is a separate judgement call. It never enters picking.
  std::vector<Instance> scaffold;
  std::array<Batch, kShapeSlots> scaffoldBatches{};

  [[nodiscard]] std::size_t size() const noexcept { return instances.size(); }
};

/// Draws a position. Consumes an immutable PositionView, never a live Position, and
/// never generates a move of its own - a highlight it draws came from the engine.
class BoardRenderer {
 public:
  static Result<BoardRenderer> create(
      const VulkanContext& ctx, VkFormat colorFormat = OffscreenTarget::kColorFormat);

  /// How many frames may be in flight at once. Two: the interactive path renders into a
  /// freshly acquired swapchain image while the previous frame is still being presented,
  /// which is the whole of M4.8's pipelining (ADR-0018).
  static constexpr std::uint32_t kFramesInFlight = 2;

  BoardRenderer() = default;
  ~BoardRenderer();
  BoardRenderer(const BoardRenderer&) = delete;
  BoardRenderer& operator=(const BoardRenderer&) = delete;
  BoardRenderer(BoardRenderer&& o) noexcept { *this = std::move(o); }
  BoardRenderer& operator=(BoardRenderer&& o) noexcept;

  [[nodiscard]] const view::Theme& theme() const noexcept { return theme_; }
  void setTheme(const view::Theme& t) noexcept { theme_ = t; }
  /// How far out of focus the board is, 0 to 1.
  ///
  /// Pause steps the camera back and throws the board out of focus so the menu in front
  /// of it is what the eye lands on. Zero costs nothing at all - the passes are skipped,
  /// not run with a zero radius - so a game in play pays for none of it.
  void setBlur(float strength) noexcept { blur_ = strength; }
  [[nodiscard]] float blur() const noexcept { return blur_; }

  [[nodiscard]] const BoardOptions& options() const noexcept { return options_; }
  void setOptions(const BoardOptions& o) noexcept { options_ = o; }

  /// Cells the engine says were part of the last move, drawn with a lingering mark so a
  /// player can see what just happened without reading the ledger.
  void setLastMove(CellId from, CellId to) noexcept {
    lastFrom_ = from;
    lastTo_ = to;
  }
  /// Give the variant's authored piece bodies and icons slots in the mesh library and
  /// upload them. Cheap when nothing changed (a signature compare), and a rebuild only
  /// when a body was edited or another variant's pieces arrived. Call it before
  /// `buildInstances`; pieces whose bodies are not synced are drawn from their
  /// archetype, so skipping it is safe, just plain.
  Result<void> syncBodies(const VariantSpec& v);
  /// Royal cell to mark as under attack, or kInvalidCell.
  void setCheckCell(CellId c) noexcept { checkCell_ = c; }

  /// Turn a snapshot into instances. Pure and side-effect free, so it is unit-testable
  /// with no Vulkan at all - which is where most rendering bugs are actually found.
  /// `seams` colours the edges that are not really edges, and `anim` moves one piece
  /// off its cell while it is travelling. Both are optional: a still frame of a plain
  /// box needs neither, and the tests that cover instance building pass nullptr.
  [[nodiscard]] InstanceSet buildInstances(
      const view::PositionView& p, const view::ViewConfig& cfg,
      const view::SeamMap* seams = nullptr, const view::MoveAnimation* anim = nullptr,
      const std::function<bool(CellId)>& visible = {},
      const std::function<bool(CellId)>& present = {},
      const std::vector<view::TimelineLink>& links = {}) const;
  /// Render one frame into `target`, leaving the colour image in TRANSFER_SRC_OPTIMAL.
  /// `overlay` records extra commands inside the same render pass, which is how the UI
  /// is drawn over the board without a second path. `background` records commands at the
  /// very start of that pass, before the board's own viewport and draws, so an ambient
  /// field can be painted behind the board rather than over it; an empty one changes
  /// nothing.
  Result<void> render(const OffscreenTarget& target, const InstanceSet& instances,
                      const view::OrbitCamera& camera,
                      const std::function<void(VkCommandBuffer)>& overlay = {},
                      const std::function<void(VkCommandBuffer)>& background = {},
                      BoardRect boardRect = {});

  /// Record one frame into a command buffer the caller owns and submits. Nothing is
  /// waited on, so the caller can keep `kFramesInFlight` frames overlapping; `frame`
  /// selects the per-frame instance buffer and blur descriptors, and must be
  /// `0 .. kFramesInFlight-1`. The colour image is left in COLOR_ATTACHMENT_OPTIMAL for
  /// the caller's own final transition. `render` above is the synchronous wrapper used by
  /// captures and tests, which leaves the image in TRANSFER_SRC_OPTIMAL.
  Result<void> record(VkCommandBuffer cmd, const OffscreenTarget& target,
                      const InstanceSet& instances, const view::OrbitCamera& camera,
                      const std::function<void(VkCommandBuffer)>& overlay,
                      const std::function<void(VkCommandBuffer)>& background,
                      BoardRect boardRect, std::uint32_t frame);

  Result<Image> renderToImage(const OffscreenTarget& target, const view::PositionView& p,
                              const view::ViewConfig& cfg,
                              const view::OrbitCamera& camera);

  [[nodiscard]] static view::Vec3 cellHalfExtent();

 private:
  Result<void> buildPipeline();
  Result<void> buildBackdropPipeline();
  Result<void> buildBlurPipeline();
  /// Point the frame's two descriptor sets at this target's images. Rewritten only when
  /// the target's colour view changes; the frame slot's fence is what makes that safe
  /// while the previous frame of the same slot may still have been reading them.
  Result<void> bindBlurTarget(const OffscreenTarget& target, std::uint32_t frame);
  Result<void> uploadGeometry();
  Result<void> ensureInstanceCapacity(std::uint32_t frame, std::size_t count);
  Result<void> ensureSurfaceCapacity(std::uint32_t frame, std::size_t vertices,
                                     std::size_t indices);

  const VulkanContext* ctx_{nullptr};
  VkFormat colorFormat_{OffscreenTarget::kColorFormat};
  view::Theme theme_{view::Theme::manifold()};
  BoardOptions options_{};
  MeshLibrary meshes_;
  /// Which custom slot each piece type of the synced variant draws from (-1 for none),
  /// and the signature of the bodies that built them.
  std::vector<int> customSlot_;
  std::uint64_t bodiesSig_{0};
  CellId lastFrom_{kInvalidCell};
  CellId lastTo_{kInvalidCell};
  CellId checkCell_{kInvalidCell};

  VkShaderModule vert_{VK_NULL_HANDLE};
  VkShaderModule frag_{VK_NULL_HANDLE};
  VkPipelineLayout layout_{VK_NULL_HANDLE};
  VkPipeline pipeline_{VK_NULL_HANDLE};
  /// The same shaders and vertex input as `pipeline_`, with alpha blending and no depth
  /// writes - the geometry view's ghost board (M17.10). The opaque path is unchanged.
  VkPipeline surfaceBlendPipeline_{VK_NULL_HANDLE};

  // The lit ground the board stands on, drawn first across the whole frame.
  VkShaderModule backdropVert_{VK_NULL_HANDLE};
  VkShaderModule backdropFrag_{VK_NULL_HANDLE};
  VkPipelineLayout backdropLayout_{VK_NULL_HANDLE};
  VkPipeline backdropPipeline_{VK_NULL_HANDLE};

  // The defocus pass. One pipeline, one sampler, and two descriptor sets that ping-pong
  // between the target's two colour images.
  VkShaderModule blurVert_{VK_NULL_HANDLE};
  VkShaderModule blurFrag_{VK_NULL_HANDLE};
  VkPipelineLayout blurLayout_{VK_NULL_HANDLE};
  VkPipeline blurPipeline_{VK_NULL_HANDLE};
  VkDescriptorSetLayout blurSetLayout_{VK_NULL_HANDLE};
  VkDescriptorPool blurPool_{VK_NULL_HANDLE};
  VkSampler blurSampler_{VK_NULL_HANDLE};
  /// Two sets per frame in flight - one sampling the colour image, one the scratch -
  /// because a frame in flight must not have its descriptors rewritten underneath it.
  std::array<VkDescriptorSet, kFramesInFlight> blurFromColor_{};
  std::array<VkDescriptorSet, kFramesInFlight> blurFromScratch_{};
  std::array<VkImageView, kFramesInFlight> blurBoundColor_{};
  float blur_{0.0f};

  VkBuffer vertexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory vertexMem_{VK_NULL_HANDLE};
  VkBuffer indexBuffer_{VK_NULL_HANDLE};
  VkDeviceMemory indexMem_{VK_NULL_HANDLE};

  /// The instance buffer is per frame in flight: frame N+1 is uploaded while frame N's
  /// draws may still be reading it.
  std::array<VkBuffer, kFramesInFlight> instanceBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> instanceMem_{};
  std::array<std::size_t, kFramesInFlight> instanceCapacity_{};

  /// And so is the geometry view's board mesh, for the same reason: it is rebuilt every
  /// frame, because the shape can be slid or turned every frame.
  std::array<VkBuffer, kFramesInFlight> surfaceVertexBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> surfaceVertexMem_{};
  std::array<std::size_t, kFramesInFlight> surfaceVertexCapacity_{};
  std::array<VkBuffer, kFramesInFlight> surfaceIndexBuffer_{};
  std::array<VkDeviceMemory, kFramesInFlight> surfaceIndexMem_{};
  std::array<std::size_t, kFramesInFlight> surfaceIndexCapacity_{};
};

}  // namespace cb::render
