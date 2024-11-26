NEON_DIR=${NEON_DIR:-"/home/nonroot/neon"}
PG_INSTALL_DIR=${PG_INSTALL_DIR:-"${NEON_DIR}/pg_install"}
VERSION=${VERSION:-"latest"}
WORK_DIR=${WORK_DIR:-"${PWD}"}
OUTPUT_DIR=${OUTPUT_DIR:-"${WORK_DIR}/.output"}

export PATH=${PG_INSTALL_DIR}/bin:$PATH

make BUNDLE_LLVM=1 -j32 install

cd ${NEON_DIR}
zip -r neon-rusticadb-${VERSION}.zip *
mkdir -p ${OUTPUT_DIR}
cp neon-rusticadb-${VERSION}.zip ${OUTPUT_DIR}/

