import os
import sys

BDF_FILE = "misaki_gothic.bdf"
OUTPUT_HEADER = "misaki_font.h"

# 抽出したい文字セット（英数字、半角/全角記号、ひらがな全種）
ALPHABET = " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"
HIRAGANA = "ぁあぃいぅうぇえぉおかがきぎくぐけげこごさざしじすずせぜそぞただちぢっつづてでとどなにぬねのはばぱひびぴふびぴへべぺほぼぽまみむめもゃやゅゆょよらりるれろゎわゐゑをん"

# 抽出ターゲット（全文字を合体）
TARGET_TEXT = ALPHABET + HIRAGANA


def parse_bdf_simple(bdf_path):
  """BITMAPセクションの8行をそのまま素直に読み込む"""
  glyphs = {}
  current_encoding = None
  in_bitmap = False
  bitmap_lines = []

  if not os.path.exists(bdf_path):
    print(f"エラー: '{bdf_path}' が見つかりません。")
    sys.exit(1)

  with open(bdf_path, 'r', encoding='utf-8', errors='ignore') as f:
    for line in f:
      line = line.strip()
      if line.startswith('ENCODING'):
        parts = line.split()
        if len(parts) > 1:
          current_encoding = int(parts[1])
      elif line == 'BITMAP':
        in_bitmap = True
        bitmap_lines = []
      elif line == 'ENDCHAR':
        in_bitmap = False
        if current_encoding is not None:
          # 8行に満たない場合は後ろを0埋め、8行を超える場合は8行で切る
          grid = [0] * 8
          for idx, hex_str in enumerate(bitmap_lines[:8]):
            grid[idx] = int(hex_str, 16)
          glyphs[current_encoding] = grid
        current_encoding = None
      elif in_bitmap:
        bitmap_lines.append(line)

  return glyphs


def main():
  print(f'[{BDF_FILE}] からフォントデータを抽出中...')
  glyphs = parse_bdf_simple(BDF_FILE)

  # 重複除去
  unique_chars = []
  for char in TARGET_TEXT:
    if char not in unique_chars:
      unique_chars.append(char)

  with open(OUTPUT_HEADER, 'w', encoding='utf-8') as f:
    f.write('#ifndef MISAKI_FONT_H\n#define MISAKI_FONT_H\n\n')
    f.write('#include <stdint.h>\n#include <wchar.h>\n\n')

    # 各文字のデータ構造体
    f.write('typedef struct {\n')
    f.write('    wchar_t code;      // Unicode文字コード\n')
    f.write('    uint8_t data[8];   // 8x8ドットデータ\n')
    f.write('} FontItem;\n\n')

    f.write('static const FontItem font_table[] = {\n')

    extracted_count = 0
    for char in unique_chars:
      code = ord(char)
      if code in glyphs:
        data = glyphs[code]
        data_str = ', '.join([f'0x{b:02X}' for b in data])
        # Unicodeコードポイントをエスケープ形式で出力
        f.write(f'    {{ L\'\\u{code:04X}\', {{ {data_str} }} }}, // \'{char}\'\n')
        extracted_count += 1

    f.write('};\n\n')
    f.write(
        f'#define FONT_TABLE_SIZE (sizeof(font_table) / sizeof(FontItem))\n\n'
    )

    # 全角空白（0x3000）用のデフォルトデータ
    f.write('static const uint8_t font_blank[8] = {0};\n\n')

    # 文字からドットデータを検索するヘルパー関数
    f.write(
        'static inline const uint8_t* get_font_data(wchar_t code) {\n'
        '    for (size_t i = 0; i < FONT_TABLE_SIZE; i++) {\n'
        '        if (font_table[i].code == code) return font_table[i].data;\n'
        '    }\n'
        '    return font_blank;\n'
        '}\n\n'
    )

    f.write('#endif // MISAKI_FONT_H\n')

  print(
      f'★ 成功: {extracted_count} 文字のフォントデータを [{OUTPUT_HEADER}]'
      ' に出力しました！'
  )


if __name__ == '__main__':
  main()