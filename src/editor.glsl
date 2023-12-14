
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

uniform fs_params {
  float smoothness;
  int effect;
};

in vec4 color;
in vec2 uv;

out vec4 frag_color;

void main() {
  if (effect == 0) {
    float d = texture(sampler2D(tex, smp), uv).r;
    float aaf = fwidth(d);
    float alpha = smoothstep(0.5 - aaf, 0.5 + aaf, d);
    frag_color = vec4(color.rgb, alpha);
  }
  else if (effect == 1) {
    float d = texture(sampler2D(tex, smp), uv).r;
    float alpha = smoothstep(0.5 - smoothness, 0.5 + smoothness, d);
    frag_color = vec4(color.rgb, alpha);
  }
  else if (effect == 2) {
    float d = texture(sampler2D(tex, smp), uv).r;
    float alpha = 0;
    if (d < 0.5) {
      alpha = smoothstep(0.5 - smoothness, 0.5 + smoothness, d);
    }
    else {
      alpha = .5 - smoothstep(0.5 - smoothness, 0.5 + smoothness, .75 * d);
    }
    frag_color = vec4(color.rgb, alpha);
  }
  else if (effect == 3) {
    frag_color = texture(sampler2D(tex, smp), uv).r * vec4(1, 1, 1, 1);
  }
  else {
    frag_color = vec4(color.rgb, 1);
  }
}
@end

@program editor vs fs
