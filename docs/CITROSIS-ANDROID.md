# Citrosis Android

Citrosis has two source lines in **GRID0-net/GRID0-citron**:

| Branch | Purpose | Base |
| --- | --- | --- |
| `main` | Desktop and existing multiplatform builds | Citron Neo |
| [`codex/citrosis-android`](https://github.com/GRID0-net/GRID0-citron/tree/codex/citrosis-android) | Current Android edition | Lemon Project / Eden |

The Android tree retains upstream history and license notices, with Citrosis
memory safeguards, GRID0+ integration, branding and Splatoon 3 compatibility fixes.
The user reported the latest Android build working on 2026-10-16 after restoring
ssl:s certificate/context behavior and NIFM readiness updates. Publication does
not constitute independent gameplay, performance or CI verification.

## Clone and build

Use a separate checkout so desktop and Android build outputs stay independent:

```sh
git clone --single-branch --branch codex/citrosis-android https://github.com/GRID0-net/GRID0-citron.git GRID0-citrosis-android
cd GRID0-citrosis-android
./build-citrosis-android.sh --setup --jobs 6
```

Install JDK 17 or 21 and Android SDK command-line tools first. The wrapper uses
Android 36, NDK 28.2.13676358 and CMake 3.31.6, and produces
`build-android/Citrosis-release.apk`. Read the Android branch's
[build guide](https://github.com/GRID0-net/GRID0-citron/blob/codex/citrosis-android/docs/BUILDING-CITROSIS-ANDROID.md)
and [provenance](https://github.com/GRID0-net/GRID0-citron/blob/codex/citrosis-android/citrosis-upstream.json)
for the exact requirements and source lineage.

## Manual APK action

Open **Actions → Citrosis Android APK → Run workflow**. Leave the workflow branch
on `main` and `source_ref` on `codex/citrosis-android`. An Android commit or tag can
also be supplied. The action checks out that source explicitly and rejects a
checkout without the Citrosis Android provenance marker.

The workflow builds arm64 mainline Release with JDK 21 and two native jobs, then
uploads `Citrosis-release.apk`, `SHA256SUMS`, `SOURCE_COMMIT.txt` and
`citrosis-upstream.json` as a downloadable artifact. The source receipt records
the Android commit actually built, rather than the main workflow commit.

CI uses the checked-in development signing key; production distribution should
use the project's release signing process. This action does not run on pushes,
create release tags or publish release assets. The desktop Release workflow is
unchanged. No workflow run was started as part of publishing the source.

## Maintain both source lines

Keep Android implementation changes on `codex/citrosis-android`. Do not merge its
whole root tree into main: the Android base replaces/removes desktop components
and uses different service and build APIs. Shared GRID0+ protocol, patch and
compatibility fixes should be adapted between source lines and validated on each.

Keep the Android workflow file synchronized between main and Android when its
build steps change. Each source line keeps its own build configuration and
upstream provenance. A clone of the Android branch needs no Git submodules;
its native dependencies are fetched through CPM.
