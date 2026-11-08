#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Titulo de una tarjeta de la reja: como mucho dos lineas de 18 caracteres. Lo que no
// cabe se corta con "…" al final de la segunda linea, asi cada juego ocupa su propia
// tarjeta y el texto no se pisa con el de la vecina. Funcion pura (sin interfaz), para
// poder probarla en el PC.
namespace gamehubnx::ui {

inline constexpr std::size_t kCardTitleChars = 18;
inline constexpr std::size_t kCardTitleLines = 2;

// Caracteres UTF-8 (cada codepoint cuenta uno, no cada byte).
inline std::size_t utf8Length(const std::string& text) {
    std::size_t count = 0;
    for (const unsigned char c : text)
        if ((c & 0xC0) != 0x80)
            ++count;
    return count;
}

// Los primeros `count` caracteres UTF-8 de `text`, sin partir ningun codepoint.
inline std::string utf8Prefix(const std::string& text, std::size_t count) {
    std::size_t taken = 0;
    std::size_t i = 0;
    while (i < text.size() && taken < count) {
        ++i;
        while (i < text.size() && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80)
            ++i;
        ++taken;
    }
    return text.substr(0, i);
}

inline std::string cardTitleText(const std::string& title) {
    const std::string ellipsis = "\xE2\x80\xA6";  // …

    std::vector<std::string> words;
    std::string word;
    for (const char c : title) {
        if (c == ' ' || c == '\t' || c == '\n') {
            if (!word.empty()) {
                words.push_back(word);
                word.clear();
            }
        } else {
            word.push_back(c);
        }
    }
    if (!word.empty())
        words.push_back(word);

    std::vector<std::string> lines;
    std::string current;
    bool overflow = false;
    for (std::string piece : words) {
        // Una palabra mas larga que una linea se parte en trozos de 18.
        while (utf8Length(piece) > kCardTitleChars) {
            if (!current.empty()) {
                lines.push_back(current);
                current.clear();
                if (lines.size() == kCardTitleLines) {
                    overflow = true;
                    break;
                }
            }
            const std::string head = utf8Prefix(piece, kCardTitleChars);
            lines.push_back(head);
            piece = piece.substr(head.size());
            if (lines.size() == kCardTitleLines) {
                overflow = true;
                break;
            }
        }
        if (overflow)
            break;
        if (current.empty()) {
            current = piece;
            continue;
        }
        if (utf8Length(current) + 1 + utf8Length(piece) <= kCardTitleChars) {
            current += " " + piece;
            continue;
        }
        lines.push_back(current);
        if (lines.size() == kCardTitleLines) {
            overflow = true;
            break;
        }
        current = piece;
    }
    if (!overflow && !current.empty())
        lines.push_back(current);

    if (overflow && !lines.empty()) {
        std::string& last = lines.back();
        if (utf8Length(last) >= kCardTitleChars)
            last = utf8Prefix(last, kCardTitleChars - 1) + ellipsis;
        else
            last += ellipsis;
    }

    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        if (i > 0)
            text += '\n';
        text += lines[i];
    }
    return text;
}

}  // namespace gamehubnx::ui
