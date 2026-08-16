#pragma once

#include "EditorPanel.h"
#include "core/CollisionMatrix.h"

class Scene;

// Scene-wide collision layer matrix editor (Phase 36).
//
// A 16 x 16 togglable grid over the scene's CollisionMatrix: each row is a
// layer (its name is editable inline) and the columns are every other layer;
// a checked cell (i, j) means bodies on layers i and j interact in the physics
// step. The matrix is symmetric, so toggling a cell flips both entries, and
// the diagonal controls self-collision (uncheck it to let, say, projectiles
// pass through each other). The grid edits the Scene's matrix directly — it is
// global scene state, so changes apply immediately and persist with the scene
// file (no per-entity undo transaction, like other global settings).
class CollisionMatrixPanel : public EditorPanel
{
public:
    explicit CollisionMatrixPanel(Scene *scene);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    Scene *m_scene;
    char m_name_buffers[CollisionMatrix::kLayerCount][64] = {};
    bool m_visible = true;
};
