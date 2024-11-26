VERSION=${VERSION:-"latest"}
OUTPUT_DIR=${OUTPUT_DIR:-"${PWD}/.output"}
FILE_NAME=${OUTPUT_DIR}/neon-rusticadb-${VERSION}.zip

# check if file exists
if [ ! -f ${FILE_NAME} ]; then
    echo "File ${FILE_NAME} does not exist"
    exit 1
fi

echo "Uploading ${FILE_NAME} to obs://yanjitech-deploy/neon-rusticadb/"

obsutil cp ${FILE_NAME} obs://yanjitech-deploy/neon-rusticadb/
