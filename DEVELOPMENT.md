# AI Caption Plugin development

Проект собирает Windows x64 OBS 32.2.1 plugin с единственным локальным движком sherpa-onnx T-One.

## Требования

- Visual Studio 2022 Build Tools с workload C++;
- Windows SDK 10.0.22621 или новее;
- CMake 3.28 или новее;
- PowerShell 7 (`pwsh`);
- Git.

API-ключи, Google SDK и Ninja не требуются.

## Сборка и тесты

```powershell
cmake --fresh --preset windows-x64
cmake --build --preset windows-x64 --parallel
ctest --test-dir build_x64 -C RelWithDebInfo --output-on-failure
```

Полная упаковка:

```powershell
.\scripts\build-windows.ps1
```

Тестовый набор проверяет:

- контракт локального caption engine и ошибку отсутствующей модели;
- UTF-8, оформление и SSE-жизненный цикл Browser Source;
- замены частых английских названий;
- синтаксис, integrity-проверки и лимит кэша установщика.
- закрепление GitHub Actions, повторную проверку dependency-кэша и наличие OSS-лицензий.

## Runtime-контракт

- только CPU, один ONNX Runtime inference thread;
- bounded audio queue до одной секунды;
- входная очередь сохраняет финальные фразы и объединяет частые промежуточные результаты; её жёсткий предел — 32 элемента;
- очередь каждого медленного файлового/stream-получателя хранит только последнюю ожидающую подпись;
- worker thread с below-normal priority;
- распознавание запускается только при наличии stream, browser, file или preview consumer;
- browser consumer жив, пока открыто SSE-соединение внутри случайного защищённого URL; незащищённого `/events` нет;
- локальная модель находится в `models/sherpa-onnx-streaming-t-one-russian-2025-09-08` рядом с данными плагина.

## Перед выпуском

1. Собрать проект с чистым `build_x64`.
2. Запустить все CTest.
3. Проверить первый install и update с сохранением модели.
4. Загрузить DLL в изолированном OBS 32.2.1.
5. Проверить микрофон отдельно для Browser Source, файла и активного стрима.

## CI и релизы

- PR в `master` собирается один раз событием `pull_request`; отдельные `push`-сборки веток отключены.
- Merge или прямой push в `master` один раз собирает, тестирует, анализирует C/C++ через CodeQL и упаковывает плагин, затем создаёт тег и GitHub Release `v<version>` из `buildspec.json` в том же workflow.
- Тег вручную создавать не нужно: tag-push намеренно не запускает вторую сборку.
- Ручной `workflow_dispatch` проверяет сборку и сохраняет artifact, но не публикует релиз.
- Все внешние Actions закреплены на полных commit SHA. Dependabot предлагает их обновления отдельными PR.
- CI-кеширует только повторно проверяемые по SHA-256 архивы зависимостей. Полные каталоги сборки не кешируются, поэтому кеш остаётся меньше 1 ГиБ и не переносит path-specific CMake state между runner-ами.
