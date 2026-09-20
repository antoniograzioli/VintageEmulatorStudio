#!/bin/sh
set -euo pipefail

echo Signing Identity:	${EXPANDED_CODE_SIGN_IDENTITY_NAME:-${CODE_SIGN_IDENTITY}}

entitlementsFile="${TARGET_TEMP_DIR}/${FULL_PRODUCT_NAME}.xcent"

if [[ ! -f "${entitlementsFile}" ]]; then
  entitlementsFile=""
fi

echo Entitlements File:	${entitlementsFile:-None}
echo
echo Running codesign --force --sign \"${EXPANDED_CODE_SIGN_IDENTITY:-${CODE_SIGN_IDENTITY}}\" --verbose=4 --timestamp  ${entitlementsFile:+--entitlements \"${entitlementsFile}\"} --generate-entitlement-der \"${CODESIGNING_FOLDER_PATH}\"
codesign --force --sign "${EXPANDED_CODE_SIGN_IDENTITY:-${CODE_SIGN_IDENTITY}}" --verbose=4 --timestamp  ${entitlementsFile:+--entitlements "${entitlementsFile}"} --generate-entitlement-der "${CODESIGNING_FOLDER_PATH}"
echo
echo Running codesign --verify --deep --verbose=4 \"${CODESIGNING_FOLDER_PATH}\"
codesign --verify --deep --verbose=4 "${CODESIGNING_FOLDER_PATH}"

