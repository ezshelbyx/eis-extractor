# Architecture

## Modules
- `src/core`: types, utils, settings, logger.
- `src/archive`: archive listing types and service.
- `src/backends`: libarchive and 7-Zip backends + registry.
- `src/extraction`: extraction options and task.
- `src/queue`: bounded job queue.
- `src/integrity`: integrity checker.
- `src/security`: PathGuard.
- `src/ui`: pages and main window.
- `src/widgets`: custom painted widgets.
- `src/settings`: app settings.
- `src/logging`: logger.

## Safety
PathGuard validates every entry path, rejects traversal and absolute paths, validates symlink/hardlink targets, handles collisions, enforces limits and bomb guard.

## Concurrency
JobQueue owns jobs and a QThreadPool with configurable concurrency. ExtractionTask is QObject + QRunnable with autoDelete false; signals queued to main thread.
