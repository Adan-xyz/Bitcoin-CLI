{ pkgs }: {
  deps = [
    pkgs.gcc
    pkgs.gnumake
    pkgs.pkg-config
    pkgs.openssl
    pkgs.openssl.dev
    pkgs.db
    pkgs.db.dev
  ];
}
