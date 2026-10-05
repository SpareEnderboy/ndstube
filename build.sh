make "$@"
if [[ -d "$HOME/ds-shop/roms" ]]; then
	cp -v ndstube.nds "$HOME/ds-shop/roms/ndstube.nds"
fi
melonDS ndstube.nds