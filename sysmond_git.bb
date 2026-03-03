SUMMARY = "System Monitoring Daemon for FRDM-IMX93"
DESCRIPTION = "Monitors CPU usage and temperature, logs via syslog. \
               K2/K3 buttons control LED1 RGB color indication."
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://LICENSE;md5=<UPDATE_AFTER_ADDING_LICENSE_FILE>"

SRC_URI = "git://github.com/<YOUR_GITHUB_USERNAME>/sysmond.git;protocol=https;branch=main"
SRCREV = "${AUTOREV}"

S = "${WORKDIR}/git"

DEPENDS = "libgpiod"
RDEPENDS:${PN} = "libgpiod"

inherit systemd

SYSTEMD_SERVICE:${PN} = "sysmond.service"
SYSTEMD_AUTO_ENABLE = "enable"

do_compile() {
    oe_runmake CC="${CC}" CFLAGS="${CFLAGS}" LDFLAGS="${LDFLAGS}"
}

do_install() {
    oe_runmake install \
        DESTDIR="${D}" \
        PREFIX="${prefix}" \
        BINDIR="${sbindir}" \
        CONFDIR="${sysconfdir}" \
        SYSTEMD_DIR="${systemd_system_unitdir}"
}

FILES:${PN} += "${sysconfdir}/sysmond.conf"
FILES:${PN} += "${systemd_system_unitdir}/sysmond.service"
