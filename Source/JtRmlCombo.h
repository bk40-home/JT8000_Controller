/*
 * JtRmlCombo.h — <combo>, a selector that shows its current value
 *
 * WHY IT WAITED
 *   Every other control could be drawn from artwork alone. A combo cannot: its
 *   whole job is to tell you which option is selected, and that is text. It was
 *   held back until JtRmlFont existed rather than shipped showing a blank bar.
 *
 * OPTIONS COME FROM THE SKIN
 *   The option strings are written into the document as an `options` attribute
 *   by gen_skin.py, taken from params.yaml. The element therefore needs no
 *   knowledge of the parameter model - consistent with every other control
 *   here, which reports a normalised value and an opaque key and nothing more.
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
    void refreshText();
    void openList();
    void closeList();

    Rml::String m_param;
    std::vector<std::string> m_options;
    int m_index = 0;

    /* Owned by the document once created; cleared in closeList(). Held as a raw
     * pointer because RmlUi owns the lifetime and we only ever remove it
     * through its parent. */
    Rml::Element* m_list = nullptr;
    bool m_closePending = false;
};

} // namespace jt
