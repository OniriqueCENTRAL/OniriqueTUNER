#pragma once

#include "../GUI/Theme.h"
#include "License.h"

#include <functional>

namespace onirique::license
{

//==============================================================================
/** Ce que le plugin montre de sa licence, par-dessus son interface :

    - une pastille « MODE DÉMO · ACTIVATION » (ou « ESSAI · 5 J ») dans l'en-tête, tant que
      le plugin n'est pas acheté ;
    - la page d'activation, ouverte d'office quand le plugin n'est pas activé, puis à la
      demande en cliquant la pastille.

    Rien n'est affiché quand le plugin est acheté. Se pose en dernier sur l'interface, aux
    mêmes dimensions qu'elle ; ne capte la souris que sur la pastille et sur la page ouverte. */
class Overlay final : public juce::Component
{
public:
    Overlay (juce::String productName, std::function<Status()> readStatus, std::function<void()> refreshStatus);
    ~Overlay() override;

    /** À appeler régulièrement (une fois par seconde suffit) : suit l'état de la licence. */
    void update();

    /** Emplacement de la pastille, dans les coordonnées de l'interface. */
    void setPillBounds (juce::Rectangle<int> bounds);

    void paint (juce::Graphics&) override;
    void resized() override;
    bool hitTest (int x, int y) override;

private:
    class Pill;
    class Action;

    void setPageOpen (bool shouldBeOpen);
    juce::Rectangle<int> cardBounds() const;

    onirique::ui::Fonts fonts;
    juce::String product;
    std::function<Status()> readStatus;
    std::function<void()> refreshStatus;

    Status status;
    bool pageOpen = false;
    bool openedOnce = false;
    juce::Rectangle<int> pillBounds;

    std::unique_ptr<Pill> pill;
    std::unique_ptr<Action> openCentral, recheck, dismiss;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Overlay)
};

} // namespace onirique::license
