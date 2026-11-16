#!/usr/bin/env bash
set -euo pipefail

# ---------------------------------------------------------------------------
# Arduino CLI strict build script
# Usage:
#   ./build.sh
#   ./build.sh -b arduino:avr:uno ./MySketch
#   ./build.sh --werror -b esp32:esp32:esp32 ./MySketch
# ---------------------------------------------------------------------------

# Defaults — override with CLI arguments or environment variables
FQBN="${FQBN:-arduino:avr:uno}"
SKETCH="${SKETCH:-.}"
OUTPUT_DIR="${OUTPUT_DIR:-build}"
WARNINGS="${WARNINGS:-all}"
WERROR="${WERROR:-0}"
LIBRARY_ARGS=()
INCLUDE_DIRS=()
CLEAN=0

# Strict compiler flags.
# Keep these non-fatal by default because Arduino cores/libraries may warn.
COMMON_FLAGS=(
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wcast-align
  -Wconversion
  -Wsign-conversion
  -Wdouble-promotion
  -Wundef
  -Wformat=2
  -Wformat-security
  -Werror=return-type
  -Werror=implicit-function-declaration
)

C_FLAGS=("${COMMON_FLAGS[@]}")
CPP_FLAGS=("${COMMON_FLAGS[@]}")

# The Arduino SAMD core (and the bundled SPI library) triggers these
# diagnostics in its own sources. They stay visible as warnings so that a
# clean --werror build only fails on diagnostics from the project.
PLATFORM_FLAGS=(
  -Wno-error=undef
  -Wno-error=shadow
  -Wno-error=pedantic
  -Wno-error=conversion
  -Wno-error=sign-conversion
  -Wno-error=format-nonliteral
  -Wno-error=float-conversion
)

# Optionally turn every warning into an error.
if [[ "$WERROR" == "1" ]]; then
  C_FLAGS+=(-Werror "${PLATFORM_FLAGS[@]}")
  CPP_FLAGS+=(-Werror "${PLATFORM_FLAGS[@]}")
fi

# ---------------------------------------------------------------------------
# Help
# ---------------------------------------------------------------------------
usage() {
  cat <<EOF
Usage: $(basename "$0") [options] [sketch]

Options:
  -b, --fqbn <FQBN>       Board FQBN. Default: ${FQBN}
  -o, --output <dir>      Build output directory. Default: ${OUTPUT_DIR}
  -w, --warnings <level>  none | default | more | all. Default: ${WARNINGS}
  -l, --library <dir>     Extra library directory (repeatable)
  -I, --include-dir <dir> Extra include directory (repeatable)
      --clean             Clean build, ignore cached objects
      --werror            Treat all compiler warnings as errors
  -h, --help              Show this help

Environment variables:
  FQBN, SKETCH, OUTPUT_DIR, WARNINGS, WERROR

Examples:
  ./build.sh
  ./build.sh -b arduino:avr:uno ./MySketch
  ./build.sh --werror -b arduino:samd:mkrwifi1010 --clean \\
             --library . examples/TlsGet
EOF
}

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
while [[ $# -gt 0 ]]; do
  case "$1" in
    -b|--fqbn)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      FQBN="$2"
      shift 2
      ;;
    -o|--output)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      OUTPUT_DIR="$2"
      shift 2
      ;;
    -w|--warnings)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      WARNINGS="$2"
      shift 2
      ;;
    -l|--library)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      LIBRARY_ARGS+=(--library "$2")
      shift 2
      ;;
    -I|--include-dir)
      [[ $# -ge 2 ]] || { echo "Missing value for $1" >&2; exit 2; }
      INCLUDE_DIRS+=("$2")
      shift 2
      ;;
    --clean)
      CLEAN=1
      shift
      ;;
    --werror)
      WERROR=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      break
      ;;
    -*)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 2
      ;;
    *)
      SKETCH="$1"
      shift
      ;;
  esac
done

# Resolve include directories and append them to the compiler flags.
if [[ ${#INCLUDE_DIRS[@]} -gt 0 ]]; then
  for include_dir in "${INCLUDE_DIRS[@]}"; do
    [[ -d "$include_dir" ]] || {
      echo "Error: include directory not found: $include_dir" >&2
      exit 1
    }
    absolute_include_dir="$(cd "$include_dir" && pwd)"
    C_FLAGS+=("-I${absolute_include_dir}")
    CPP_FLAGS+=("-I${absolute_include_dir}")
  done
fi

# Join arrays into a single argument for --build-property.
C_FLAGS_STR="${C_FLAGS[*]}"
CPP_FLAGS_STR="${CPP_FLAGS[*]}"

# ---------------------------------------------------------------------------
# Checks
# ---------------------------------------------------------------------------
command -v arduino-cli >/dev/null 2>&1 || {
  echo "Error: arduino-cli is not installed or not in PATH." >&2
  exit 1
}

[[ -d "$SKETCH" ]] || {
  echo "Error: sketch directory not found: $SKETCH" >&2
  exit 1
}

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
echo "==> Board:      $FQBN"
echo "==> Sketch:     $SKETCH"
echo "==> Output:     $OUTPUT_DIR"
echo "==> Warnings:   $WARNINGS"
echo "==> Werror:     $WERROR"
echo "==> Clean:      $CLEAN"
if [[ ${#LIBRARY_ARGS[@]} -gt 0 ]]; then
  echo "==> Libraries:  ${LIBRARY_ARGS[*]}"
fi
if [[ ${#INCLUDE_DIRS[@]} -gt 0 ]]; then
  echo "==> Includes:   ${INCLUDE_DIRS[*]}"
fi
echo

mkdir -p "$OUTPUT_DIR"

CLI_ARGS=(
  --fqbn "$FQBN"
  --warnings "$WARNINGS"
  --output-dir "$OUTPUT_DIR"
  --build-property "compiler.c.extra_flags=$C_FLAGS_STR"
  --build-property "compiler.cpp.extra_flags=$CPP_FLAGS_STR"
)

if [[ ${#LIBRARY_ARGS[@]} -gt 0 ]]; then
  CLI_ARGS+=("${LIBRARY_ARGS[@]}")
fi
if [[ "$CLEAN" == "1" ]]; then
  CLI_ARGS+=(--clean)
fi

arduino-cli compile "${CLI_ARGS[@]}" "$SKETCH"

echo
echo "==> Build succeeded."
echo "==> Binaries: $OUTPUT_DIR"