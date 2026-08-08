#pragma once

#include "EditorPanel.h"

#include <string>

class CommandHistory;

// History panel (Phase 22): a read-only view of the editor's undo/redo stacks
// with Undo / Redo / Clear buttons. Useful for understanding what the current
// Ctrl+Z / Ctrl+Y sequence will do; it shares the same CommandHistory instance
// every other panel uses, so it always reflects the live stacks.
class HistoryPanel : public EditorPanel
{
public:
    explicit HistoryPanel(CommandHistory *history);

    void OnImGuiRender(float dt) override;

    void ToggleVisible() { m_visible = !m_visible; }
    bool IsVisible() const { return m_visible; }
    void SetVisible(bool visible) { m_visible = visible; }

private:
    CommandHistory *m_history;
    bool m_visible = false;
};
