# NScoffee

Кофе-таймер

## сборка

```bash
gcc -o coffee_timer coffee_timer.c -I/usr/include/SDL2 -lSDL2 -lSDL2_mixer
```

## Использование

Запускать с аргументами: программа время_работы музыкальный_файл
Музывальный файл не более 10 секунд для удобства

```bash
./coffee_timer 10 /путь/к/вашему/файлу.mp3
```