#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <string.h>
#include <errno.h>

#define WINDOW_WIDTH  206
#define WINDOW_HEIGHT 173
#define DEFAULT_TIMER_SECONDS 490
#define DEFAULT_MUSIC_FILE    "/home/diver/Загрузки/Музыка/12/coffee.mp3"
#define LOCK_FILE     "/tmp/coffee_timer.lock"

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
Mix_Music* music = NULL;
int lock_fd = -1;

// Прототипы функций
void init_sdl();
void cleanup();
void render_progress(float progress);
int try_get_lock();
void check_exit_signal();

/*----------------------------------------------------------
  Инициализация SDL и аудио
----------------------------------------------------------*/
void init_sdl() {
    // Инициализация SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        exit(1);
    }

    // Инициализация аудио
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mixer error: %s\n", Mix_GetError());
        SDL_Quit();
        exit(1);
    }

    // Создание окна
    window = SDL_CreateWindow("Coffee Timer", 
                             SDL_WINDOWPOS_CENTERED, 
                             SDL_WINDOWPOS_CENTERED,
                             WINDOW_WIDTH, 
                             WINDOW_HEIGHT, 
                             SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Window error: %s\n", SDL_GetError());
        Mix_CloseAudio();
        SDL_Quit();
        exit(1);
    }

    // Создание рендерера
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        Mix_CloseAudio();
        SDL_Quit();
        exit(1);
    }
}

/*----------------------------------------------------------
  Отрисовка прогресс-бара
----------------------------------------------------------*/
void render_progress(float progress) {
    // Очистка экрана
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);

    // Рисуем прогресс-бар
    SDL_SetRenderDrawColor(renderer, 70, 200, 70, 255);
    SDL_Rect bar = {
        .x = 20,
        .y = 40,
        .w = (int)((WINDOW_WIDTH - 40) * progress),
        .h = 20
    };
    SDL_RenderFillRect(renderer, &bar);

    // Выводим на экран
    SDL_RenderPresent(renderer);
}

/*----------------------------------------------------------
  Получение блокировки файла
----------------------------------------------------------*/
int try_get_lock() {
    // Открываем/создаем файл блокировки
    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (lock_fd == -1) {
        perror("Lock file error");
        return 0;
    }

    // Пытаемся получить эксклюзивную блокировку
    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            // Файл уже заблокирован - записываем команду выхода
            const char exit_cmd[] = "exit";
            write(lock_fd, exit_cmd, sizeof(exit_cmd));
            fsync(lock_fd);
            close(lock_fd);
            printf("Программа уже запущена. Отправлен сигнал выхода.\n");
        } else {
            perror("Lock failed");
            close(lock_fd);
        }
        return 0;
    }

    // Успешно получили блокировку
    ftruncate(lock_fd, 0);
    const char working[] = "working";
    write(lock_fd, working, sizeof(working));
    fsync(lock_fd);
    return 1;
}

/*----------------------------------------------------------
  Проверка команды выхода
----------------------------------------------------------*/
void check_exit_signal() {
    char status[10] = {0};
    lseek(lock_fd, 0, SEEK_SET);
    read(lock_fd, status, sizeof(status));

    if (strcmp(status, "exit") == 0) {
        printf("Получена команда выхода. Завершение...\n");
        cleanup();
        exit(0);
    }
}

/*----------------------------------------------------------
  Очистка ресурсов
----------------------------------------------------------*/
void cleanup() {
    if (music) Mix_FreeMusic(music);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    Mix_CloseAudio();
    SDL_Quit();

    if (lock_fd != -1) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        unlink(LOCK_FILE);
    }
}

/*----------------------------------------------------------
  Главная функция
----------------------------------------------------------*/
int main(int argc, char* argv[]) {
    // Установка значений по умолчанию
    int timer_seconds = DEFAULT_TIMER_SECONDS;
    const char* music_file = DEFAULT_MUSIC_FILE;

    // Обработка аргументов командной строки
    if (argc >= 2) {
        char* endptr;
        long seconds = strtol(argv[1], &endptr, 10);
        
        if (*endptr != '\0' || seconds <= 0) {
            fprintf(stderr, "Invalid timer value. Using default: %d seconds\n", 
                    DEFAULT_TIMER_SECONDS);
        } else {
            timer_seconds = (int)seconds;
        }
    }
    
    if (argc >= 3) {
        music_file = argv[2];
    }

    printf("Using configuration:\n");
    printf("  Timer: %d seconds\n", timer_seconds);
    printf("  Music: %s\n", music_file);

    // Проверка блокировки
    if (!try_get_lock()) {
        return 1;
    }

    // Гарантированная очистка при выходе
    atexit(cleanup);

    // Инициализация
    init_sdl();

    // Загрузка музыки
    music = Mix_LoadMUS(music_file);
    if (!music) {
        fprintf(stderr, "Music load error: %s\n", Mix_GetError());
        return 1;
    }

    // Основной цикл
    int running = 1;
    int seconds = 0;
    SDL_Event event;

    while (running && seconds < timer_seconds) {
        // Обработка событий
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
        }

        // Проверка команды выхода
        check_exit_signal();

        // Отрисовка
        render_progress((float)seconds / timer_seconds);

        // Пауза 1 секунда
        SDL_Delay(1000);
        seconds++;
    }

    // Воспроизведение музыки
    if (running) {
        Mix_PlayMusic(music, 1);
        while (Mix_PlayingMusic()) {
            SDL_Delay(100);
            check_exit_signal();
        }
    }

    return 0;
}