#include "Widgets/keyboard_map_widget.hpp"

#include "UX/theme.hpp"

#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>

namespace canvas::gui {

namespace {

constexpr int kUnit = 46;
constexpr int kGap = 6;

int key_width(double units) {
    return static_cast<int>(units * kUnit + (units - 1.0) * kGap + 0.5);
}

}

KeyboardMapWidget::KeyboardMapWidget(QWidget* parent) : QWidget(parent) {
    rebuild_layout();
    setMinimumSize(sizeHint());
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFocusPolicy(Qt::NoFocus);
}

void KeyboardMapWidget::setBindings(
    const std::vector<canvas::core::actions::KeyBinding>& bindings) {
    marks_.clear();
    for (const auto& binding : bindings) {
        const std::string canonical = canvas::core::actions::canonical_shortcut(binding.shortcut);
        if (canonical.empty()) continue;
        const QStringList parts =
            QString::fromStdString(canonical).split(QLatin1Char('+'), Qt::SkipEmptyParts);
        if (parts.isEmpty()) continue;
        Mark mark;
        mark.key = parts.constLast();
        mark.ctrl = parts.contains(QStringLiteral("Ctrl"));
        mark.alt = parts.contains(QStringLiteral("Alt"));
        mark.shift = parts.contains(QStringLiteral("Shift"));
        mark.meta = parts.contains(QStringLiteral("Meta"));
        marks_.push_back(mark);
    }
    update();
}

void KeyboardMapWidget::setActiveShortcut(const QString& shortcut) {
    parse_shortcut(shortcut);
    update();
}

void KeyboardMapWidget::setModifier(const QString& modifier, bool on) {
    if (modifier == QStringLiteral("Ctrl")) ctrl_ = on;
    else if (modifier == QStringLiteral("Alt"))
        alt_ = on;
    else if (modifier == QStringLiteral("Shift"))
        shift_ = on;
    else if (modifier == QStringLiteral("Meta"))
        meta_ = on;
    update();
}

QString KeyboardMapWidget::activeShortcut() const {
    if (key_.isEmpty()) return {};
    return modifier_prefix() + key_;
}

QSize KeyboardMapWidget::sizeHint() const {
    int width = 0;
    int height = 0;
    for (const auto& row : rows_) {
        int x = 0;
        for (const auto& key : row) x = std::max(x, key.rect.right() + 1);
        width = std::max(width, x);
        for (const auto& key : row) height = std::max(height, key.rect.bottom() + 1);
    }
    return {width, height};
}

void KeyboardMapWidget::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const ThemeTokens& theme = tokens();
    QFont label_font = painter.font();
    label_font.setPointSize(11);
    painter.setFont(label_font);

    for (const auto& row : rows_) {
        for (const auto& key : row) {
            if (key.spacer) continue;
            int exact = 0;
            int related = 0;
            for (const auto& mark : marks_) {
                if (mark.key != key.key) continue;
                if (mark.ctrl == ctrl_ && mark.alt == alt_ && mark.shift == shift_ &&
                    mark.meta == meta_)
                    ++exact;
                else
                    ++related;
            }
            const bool modifier_on = (key.key == QStringLiteral("Ctrl") && ctrl_) ||
                                     (key.key == QStringLiteral("Alt") && alt_) ||
                                     (key.key == QStringLiteral("Shift") && shift_) ||
                                     (key.key == QStringLiteral("Meta") && meta_);
            const bool selected = !key_.isEmpty() && key.key == key_ && exact > 0;
            const bool active_key = !key_.isEmpty() && key.key == key_;
            QColor fill = theme.surface_raised;
            QColor border = theme.border_soft;
            if (exact > 0) fill = theme.accent_soft;
            if (modifier_on || active_key) fill = theme.surface_higher;
            if (selected || active_key) border = theme.accent;
            painter.setPen(QPen(border, (selected || active_key) ? 2 : 1));
            painter.setBrush(fill);
            painter.drawRoundedRect(key.rect.adjusted(1, 1, -1, -1), 7, 7);
            painter.setPen(theme.ink);
            painter.drawText(key.rect.adjusted(4, 2, -4, -4),
                             Qt::AlignHCenter | Qt::AlignVCenter | Qt::TextWordWrap, key.label);
            if (related > 0) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.ink_faint);
                const QPoint corner(key.rect.right() - 3, key.rect.top() + 3);
                QPolygon triangle;
                triangle << corner << (corner + QPoint(-9, 0)) << (corner + QPoint(0, 9));
                painter.drawPolygon(triangle);
            }
            if (exact > 1) {
                painter.setPen(Qt::NoPen);
                painter.setBrush(theme.warn);
                const QPoint corner(key.rect.left() + 3, key.rect.top() + 3);
                QPolygon triangle;
                triangle << corner << (corner + QPoint(9, 0)) << (corner + QPoint(0, 9));
                painter.drawPolygon(triangle);
            } else if (exact == 1 && !selected) {
                painter.setPen(theme.ink_muted);
                QFont count_font = painter.font();
                count_font.setPointSize(9);
                painter.setFont(count_font);
                painter.drawText(key.rect.adjusted(4, 2, -6, -4), Qt::AlignRight | Qt::AlignBottom,
                                 QStringLiteral("1"));
                painter.setFont(label_font);
            }
        }
    }
}

void KeyboardMapWidget::mousePressEvent(QMouseEvent* event) {
    if (event->button() != Qt::LeftButton) return;
    for (auto& row : rows_) {
        for (auto& key : row) {
            if (key.spacer || !key.rect.contains(event->pos())) continue;
            if (key.modifier) {
                if (key.key == QStringLiteral("Ctrl")) ctrl_ = !ctrl_;
                else if (key.key == QStringLiteral("Alt"))
                    alt_ = !alt_;
                else if (key.key == QStringLiteral("Shift"))
                    shift_ = !shift_;
                else if (key.key == QStringLiteral("Meta"))
                    meta_ = !meta_;
            } else {
                key_ = key.key;
            }
            update();
            emit shortcutSelected(activeShortcut());
            return;
        }
    }
}

void KeyboardMapWidget::rebuild_layout() {
    rows_.clear();
    const Key gap = {{}, {}, 0.35, 1, false, true, {}};
    rows_.push_back({
        {"Esc", "Escape"},
        {"F1", "F1"},
        {"F2", "F2"},
        {"F3", "F3"},
        {"F4", "F4"},
        gap,
        {"F5", "F5"},
        {"F6", "F6"},
        {"F7", "F7"},
        {"F8", "F8"},
        gap,
        {"F9", "F9"},
        {"F10", "F10"},
        {"F11", "F11"},
        {"F12", "F12"},
        gap,
        {"Insert", "Insert"},
        {"Home", "Home"},
        {"PageUp", "PageUp"},
        gap,
        {"NumLock", "NumLock"},
        {"/", "Slash"},
        {"*", "Asterisk"},
        {"-", "Minus"},
    });
    rows_.push_back({
        {"`", "QuoteLeft"},
        {"1", "1"},
        {"2", "2"},
        {"3", "3"},
        {"4", "4"},
        {"5", "5"},
        {"6", "6"},
        {"7", "7"},
        {"8", "8"},
        {"9", "9"},
        {"0", "0"},
        {"-", "Minus"},
        {"=", "Equal"},
        {"Backspace", "Backspace", 2.0},
        gap,
        {"Delete", "Delete"},
        {"End", "End"},
        {"PageDown", "PageDown"},
        gap,
        {"7", "7"},
        {"8", "8"},
        {"9", "9"},
        {"+", "Plus", 1.0, 2},
    });
    rows_.push_back({
        {"Tab", "Tab", 1.5},
        {"Q", "Q"},
        {"W", "W"},
        {"E", "E"},
        {"R", "R"},
        {"T", "T"},
        {"Y", "Y"},
        {"U", "U"},
        {"I", "I"},
        {"O", "O"},
        {"P", "P"},
        {"[", "BracketLeft"},
        {"]", "BracketRight"},
        {"\\", "Backslash", 1.5},
        gap,
        {{}, {}, 3.35, 1, false, true, {}},
        gap,
        {"4", "4"},
        {"5", "5"},
        {"6", "6"},
    });
    rows_.push_back({
        {"caps lock", "CapsLock", 1.75},
        {"A", "A"},
        {"S", "S"},
        {"D", "D"},
        {"F", "F"},
        {"G", "G"},
        {"H", "H"},
        {"J", "J"},
        {"K", "K"},
        {"L", "L"},
        {";", "Semicolon"},
        {"'", "Apostrophe"},
        {"Enter", "Enter", 2.25},
        gap,
        {{}, {}, 1.0, 1, false, true, {}},
        {"Up", "Up"},
        {{}, {}, 1.0, 1, false, true, {}},
        gap,
        {"1", "1"},
        {"2", "2"},
        {"3", "3"},
        {"Enter", "Enter", 1.0, 2},
    });
    rows_.push_back({
        {"Shift", "Shift", 2.25, 1, true},
        {"Z", "Z"},
        {"X", "X"},
        {"C", "C"},
        {"V", "V"},
        {"B", "B"},
        {"N", "N"},
        {"M", "M"},
        {",", "Comma"},
        {".", "Period"},
        {"/", "Slash"},
        {"Shift", "Shift", 2.75, 1, true},
        gap,
        {"Left", "Left"},
        {"Down", "Down"},
        {"Right", "Right"},
        gap,
        {"0", "0", 2.0},
        {".", "Period"},
    });
    rows_.push_back({
        {"Ctrl", "Ctrl", 1.25, 1, true},
        {"Meta", "Meta", 1.25, 1, true},
        {"Alt", "Alt", 1.25, 1, true},
        {"Space", "Space", 6.0},
        {"Alt", "Alt", 1.25, 1, true},
        {"Menu", "Menu", 1.25},
        {"Ctrl", "Ctrl", 1.25, 1, true},
    });

    int y = 0;
    for (auto& row : rows_) {
        int x = 0;
        for (auto& key : row) {
            const int w = key.spacer ? key_width(key.width) : key_width(key.width);
            const int h = kUnit * key.row_span + kGap * (key.row_span - 1);
            key.rect = QRect(x, y, w, h);
            x += w + kGap;
        }
        y += kUnit + kGap;
    }
    setMinimumSize(sizeHint());
}

void KeyboardMapWidget::parse_shortcut(const QString& shortcut) {
    key_.clear();
    ctrl_ = false;
    alt_ = false;
    shift_ = false;
    meta_ = false;
    const std::string canonical = canvas::core::actions::canonical_shortcut(shortcut.toStdString());
    if (canonical.empty()) return;
    const QStringList parts =
        QString::fromStdString(canonical).split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    ctrl_ = parts.contains(QStringLiteral("Ctrl"));
    alt_ = parts.contains(QStringLiteral("Alt"));
    shift_ = parts.contains(QStringLiteral("Shift"));
    meta_ = parts.contains(QStringLiteral("Meta"));
    key_ = parts.constLast();
}

QString KeyboardMapWidget::modifier_prefix() const {
    QString out;
    if (ctrl_) out += QStringLiteral("Ctrl+");
    if (alt_) out += QStringLiteral("Alt+");
    if (shift_) out += QStringLiteral("Shift+");
    if (meta_) out += QStringLiteral("Meta+");
    return out;
}

}
