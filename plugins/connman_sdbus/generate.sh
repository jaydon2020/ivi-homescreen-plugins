#!/usr/bin/env bash
# Copyright 2020-2025 Toyota Connected North America
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Re-generate proxy and adaptor headers from ConnMan D-Bus XML interface files.
# Run from the connman_sdbus/ directory:
#   ./generate.sh
#
# Requires sdbus-c++-xml2cpp to be on PATH (part of sdbus-c++ dev package).
#
# Source XML files:
#   Manager, Technology, Service — from app/sdbus-cpp-examples/interfaces/net/connman/
#   Agent                        — generated/xml/connman-agent.xml  (no upstream source)

set -euo pipefail

TOOL="sdbus-c++-xml2cpp"
# Path to the canonical XML sources shared with sdbus-cpp-examples.
UPSTREAM_XML="$(cd "$(dirname "$0")/../../../../sdbus-cpp-examples/interfaces/net/connman" && pwd)"
OUT_DIR="$(cd "$(dirname "$0")/generated" && pwd)"

command -v "${TOOL}" >/dev/null 2>&1 || {
  echo "ERROR: ${TOOL} not found on PATH." >&2
  echo "Install it with: sudo apt install libsdbus-c++-dev" >&2
  exit 1
}

echo "Using upstream XML from: ${UPSTREAM_XML}"

echo "Generating manager_proxy.h ..."
"${TOOL}" "${UPSTREAM_XML}/Manager/Manager.xml" \
  --proxy="${OUT_DIR}/manager_proxy.h"

echo "Generating technology_proxy.h ..."
"${TOOL}" "${UPSTREAM_XML}/Technology/Technology.xml" \
  --proxy="${OUT_DIR}/technology_proxy.h"

echo "Generating service_proxy.h ..."
"${TOOL}" "${UPSTREAM_XML}/Service/Service.xml" \
  --proxy="${OUT_DIR}/service_proxy.h"

echo "Generating agent_adaptor.h ..."
"${TOOL}" "${OUT_DIR}/xml/connman-agent.xml" \
  --adaptor="${OUT_DIR}/agent_adaptor.h"

echo "Done. DO NOT edit the generated/ headers by hand."
