/*
 * JtRmlCombo.h — <combo>, a selector that shows its current value
 *
 * WHY IT WAITED
 *   Every other control could be drawn from artwork alone. A combo cannot: its
 *   whole job is to tell you which option is selected, and that is text. It was
 *   held back until JtRmlFont existed rather than shipped showing a blank bar.
 *
 * OPTIONS COME FROM THE PARAMETER TABLE (N4, 2026-10-22)
 *   When `key` names a Select parameter in gen/JtParamTable.h, the option list
 *   is taken from that table and any `options` attribute in the skin is
 *   IGNORED.  Why: the normalised value is index / (count - 1), so the count
 *   MUST equal the APVTS choice count.  Skins froze the list at the time they
 *   were written; when fx.mod_effect grew from 12 to 24 options, a stale
 *   12-entry skin list sent "Super Chorus" (index 11 -> 1.0) as "Freeze Ph 2".
 *   The table is generated from params.yaml with the APVTS, so it cannot
 *   drift.  The `options` attribute is still honoured for a key the table
 *   does not know (e.g. a skin-only control), exactly as before.
 *
 * VALUE
 *   Reported as normalised 0..1 like every other control, so the host binding
 *   is uniform. index = round(value * (count - 1)). Quantising on the way in
 *   matters: a combo showing "between Chorus 2 and Chorus 3" is meaningless,
 *   and an un-quantised value would drift on every automation pass.
 *
 * THE POPUP
 *   Built as a real element in the document rather than a native menu, because
 *   a native popup in a plugin editor has to deal with the host's window, and
 *   RmlUi already knows how to lay out and clip a list.
 *
 *   It is parented to the DOCUMENT, not to the combo: a list inside the combo
 *   would be clipped by it and would inherit its size. It is removed on
 *   selection or when focus leaves.
 *
 * RCSS CONTRACT
 *   .combo-list and .combo-item must be styled by the skin. The element sets
 *   only position and size; appearance is not its business.
 */

#pragma once

#include <RmlUi/Core.h>

#include <functional>
#include <string>
#include <vector>

namespace jt
{

class JtRmlCombo final : public Rml::Element, public Rml::EventListener
{
public:
    explicit JtRmlCombo(const Rml::String& tag);
    ~JtRmlCombo() override;

    static void registerElement();

    /* Normalised, to match every other control. */
    void setNormalised(float value, bool notify = true);
    float getNormalised() const;

    void setIndex(int index, bool notify = true);
    int getIndex() const { return m_index; }
    int optionCount() const { return static_cast<int>(m_options.size()); }

    const Rml::String& getParam() const { return m_param; }

    std::function<void(JtRmlCombo&, float)> onValueChanged;
    std::function<void(JtRmlCombo&, bool)> onHover;

protected:
    void OnAttributeChange(const Rml::ElementAttributes& changed) override;
    void ProcessDefaultAction(Rml::Event& event) override;

    /* Rml::EventListener - clicks on the popup's items arrive here. */
    void ProcessEvent(Rml::Event& event) override;

    /* Where the popup is actually destroyed. Removing it from inside an event
     * frees the element being dispatched; this is the safe point. */
    void OnUpdate() override;

private:
    void parseOptions(const Rml::String& csv);
    // N4: fill m_options from JtParams::kParams when m_param is a Select row.
    // Returns false (and leaves m_options alone) for an unknown or non-Select
    // key.  Runs on attribute change only (skin load), never per frame.
    bool loadOptionsFromTable();
    void refreshText();
    void openList();
    void closeList();

    Rml::String m_param;
    std::vector<std::string> m_options;
    int m_index = 0;
    bool m_tableOptions = false;   // N4: true = list came from the table

    /* Owned by the document once created; cleared in closeList(). Held as a raw
     * pointer because RmlUi owns the lifetime and we only ever remove it
     * through its parent. */
    Rml::Element* m_list = nullptr;
    bool m_closePending = false;
};

} // namespace jt
