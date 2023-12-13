{
  description = "cbuild";

  inputs = { nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable"; };

  outputs = { self, nixpkgs }:
    let
      inherit (nixpkgs.lib) genAttrs systems;
      forAllSystems = genAttrs systems.flakeExposed;
      pkgsFor = forAllSystems (system:
        import nixpkgs { inherit system; });
    in
    {
      devShells = forAllSystems (s:
        let pkgs = pkgsFor.${s};
        in rec {
          cbuild = pkgs.mkShell {
            nativeBuildInputs = with pkgs; [ pkg-config ];
            buildInputs = with pkgs; (with pkgs.xorg; [ libX11 libXrandr libXinerama libXcursor libXi libXext ]);
            propagatedBuildInputs = [ pkgs.libGL ];
          };
          default = cbuild;
        });
    };
}
