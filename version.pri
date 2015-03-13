DEFINES += VERSION_MAJOR=0
DEFINES += VERSION_MINOR=0
DEFINES += VERSION_BUILD=1

os2 {
    DEFINES += VERSION=\'\"v$${VERSION_MAJOR}.$${VERSION_MINOR}.$${VERSION_BUILD}\"\'
} else {
    DEFINES += VERSION=\\\"v$${VERSION_MAJOR}.$${VERSION_MINOR}.$${VERSION_BUILD}\\\"
}
