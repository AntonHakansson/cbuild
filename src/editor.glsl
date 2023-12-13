
@ctype mat4 hmm_mat4

@vs vs
uniform vs_params {
    mat4 mvp;
};

in vec4 position0;
in vec4 color0;
in vec2 texcoord0;

out vec4 color;
out vec2 uv;

void main() {
  gl_Position = mvp * position0;
  color = color0;
  uv = texcoord0;
}
@end

@fs fs
uniform texture2D tex;
uniform sampler   smp;

in vec4 color;
in vec2 uv;

out vec4 frag_color;

void main() {
  float d = texture(sampler2D(tex, smp), uv).r;
  float aaf = fwidth(d);
  float alpha = smoothstep(0.5 - aaf, 0.5 + aaf, d);
  frag_color = vec4(color.rgb, alpha);
  // frag_color = vec4(uv.xy, 1.0, 1.0);
}
@end

@program editor vs fs
