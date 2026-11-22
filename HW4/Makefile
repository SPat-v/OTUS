# Makefile для сборки curl с поддержкой HTTP, HTTPS и TELNET
# Все артефакты сборки размещаются в ./HW/

BUILDDIR := HW
SRCDIR   := $(CURDIR)

# Опции configure:
CONFIGURE_OPTS := \
	--with-openssl \
	--disable-ftp \
	--disable-ftps \
	--disable-gopher \
	--disable-dict \
	--disable-tftp \
	--disable-pop3 \
	--disable-pop3s \
	--disable-imap \
	--disable-imaps \
	--disable-smtp \
	--disable-smtps \
	--disable-scp \
	--disable-sftp \
	--disable-rtsp \
	--disable-smb \
	--disable-smbs \
	--disable-ldap \
	--disable-ldaps \
	--disable-mqtt \
	--without-libssh2 \
	--without-libssh \
	--without-nghttp2 \
	--without-ngtcp2 \
	--without-nghttp3 \
	--disable-file \
	--disable-ipfs \
	--disable-ipns \
	--disable-shared \
	--enable-static \
	--disable-manual \
	--without-zlib \
	--without-brotli \
	--without-zstd 

# Цель по умолчанию
all: build

# Создание каталога сборки
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

# Запускаем ../configure изнутри HW/
configure: | $(BUILDDIR)
	cd $(BUILDDIR) && ../configure $(CONFIGURE_OPTS)

# Сборка: libcurl, curl
build: configure
	$(MAKE) -C $(BUILDDIR)/lib
	$(MAKE) -C $(BUILDDIR)/src

clean:
	$(MAKE) -C $(BUILDDIR) clean

distclean:
	$(MAKE) -C $(BUILDDIR) distclean
	rm -rf $(BUILDDIR)

.PHONY: all configure build clean distclean
