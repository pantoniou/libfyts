{ pkgs ? import <nixpkgs> {} }:
pkgs.stdenv.mkDerivation { name = "hello"; src = ./.; }
