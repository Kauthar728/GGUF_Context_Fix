#!/bin/zsh
FILE="$1"
OUT="${FILE%.c}"
clang "$FILE" -o "$OUT" \
$(pkg-config --cflags --libs gtk+-3.0) \
-lsqlite3
if [ $? -eq 0 ]; then
  echo "✔ build OK"
  echo "▶ running..."
  ./"$OUT"
else
  echo "✖ build failed"
fi
