#pragma once

#include <QWidget>

#include <QString>
#include <vector>

#include "canvas/core/actions/keymap.hpp"

namespace canvas::gui {

class KeyboardMapWidget final : public QWidget {
    Q_OBJECT
public:
    explicit KeyboardMapWidget(QWidget* parent = nullptr);

    void setBindings(const std::vector<canvas::core::actions::KeyBinding>& bindings);
    void setActiveShortcut(const QString& shortcut);
    void setModifier(const QString& modifier, bool on);
    [[nodiscard]] QString activeShortcut() const;
    [[nodiscard]] QSize sizeHint() const override;

signals:
    void shortcutSelected(const QString& shortcut);
protected:
    void paintEvent(QPaintEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
private:
    struct Key {
        QString label;
        QString key;
        double width = 1.0;
        int row_span = 1;
        bool modifier = false;
        bool spacer = false;
        QRect rect{};
    };
    struct Mark {
        QString key;
        bool ctrl = false;
        bool alt = false;
        bool shift = false;
        bool meta = false;
    };

    void rebuild_layout();
    void parse_shortcut(const QString& shortcut);
    [[nodiscard]] QString modifier_prefix() const;

    std::vector<std::vector<Key>> rows_;
    std::vector<Mark> marks_;
    QString key_;
    bool ctrl_ = false;
    bool alt_ = false;
    bool shift_ = false;
    bool meta_ = false;
};

}
