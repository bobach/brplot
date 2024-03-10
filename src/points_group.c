#include "br_plot.h"
#include "rlgl.h"


#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <math.h>
#include <string.h>

static points_group_t* points_group_init(points_group_t* g, int group_id);
points_group_t* points_group_get(points_groups_t* pg_array, int group);
static void points_group_push_point(points_group_t* g, Vector2 v);
static void points_group_deinit(points_group_t* g);
static bool points_group_realloc(points_group_t* pg, size_t new_cap);
static Color color_get(int id);
static void br_bb_expand_with_point(bb_t* bb, Vector2 v);


BR_API void points_group_push_y(points_groups_t* pg_array, float y, int group) {
  points_group_t* pg = points_group_get(pg_array, group);
  if (pg == NULL) return;
  float x = pg->len == 0 ? 0.f : (pg->points[pg->len - 1].x + 1.f);
  points_group_push_point(pg, (Vector2){ .x = x, .y = y });
}

BR_API void points_group_push_x(points_groups_t* pg_array, float x, int group) {
  points_group_t* pg = points_group_get(pg_array, group);
  if (pg == NULL) return;
  float y = pg->len == 0 ? 0.f : (pg->points[pg->len - 1].y + 1.f);
  points_group_push_point(pg, (Vector2){ .x = x, .y = y });
}

BR_API void points_group_push_xy(points_groups_t* pg_array, float x, float y, int group) {
  points_group_t* pg = points_group_get(pg_array, group);
  if (pg == NULL) return;
  points_group_push_point(pg, (Vector2){ .x = x, .y = y });
}

BR_API void points_group_empty(points_group_t* pg) {
  pg->len = 0;
  resampling2_empty(pg->resampling);
}

BR_API void points_group_clear(points_groups_t* pg, int group_id) {
  size_t len = pg->len;
  bool found = false;
  for (size_t i = 0; i < len; ++i) {
    if (pg->arr[i].group_id == group_id) {
      found = true;
      points_group_deinit(&pg->arr[i]);
    }
    if (found == true && i + 1 < len) {
      memcpy(&pg->arr[i], &pg->arr[i + 1], sizeof(points_group_t));
    }
  }
  if (found == true) {
    memset(&pg->arr[len - 1], 0, sizeof(points_group_t));
    --pg->len;
  }
}

void points_group_export(points_group_t const* pg, FILE* file) {
  for (size_t i = 0; i < pg->len; ++i) {
    Vector2 point = pg->points[i];
    fprintf(file, "%f,%f;%d\n", point.x, point.y, pg->group_id);
  }
}

void points_group_export_csv(points_group_t const* pg, FILE* file) {
  fprintf(file, "group,id,x,y\n");
  for (size_t i = 0; i < pg->len; ++i) {
    Vector2 point = pg->points[i];
    fprintf(file, "%d,%lu,%f,%f\n", pg->group_id, i, point.x, point.y);
  }
}

void points_groups_export(points_groups_t const* pg_array, FILE* file) {
  for (size_t i = 0; i < pg_array->len; ++i) {
    points_group_export(&pg_array->arr[i], file);
  }
}

void points_groups_export_csv(points_groups_t const* pg_array, FILE* file) {
  fprintf(file, "group,id,x,y\n");
  for (size_t j = 0; j < pg_array->len; ++j) {
    points_group_t* pg = &pg_array->arr[j];
    for (size_t i = 0; i < pg->len; ++i) {
      Vector2 point = pg->points[i];
      fprintf(file, "%d,%lu,%f,%f\n", pg->group_id, i, point.x, point.y);
    }
  }
}

void points_groups_deinit(points_groups_t* arr) {
  if (arr->arr == NULL) return;
  for (size_t i = 0; i < arr->len; ++i) {
    points_group_deinit(&arr->arr[i]);
  }
  arr->len = arr->cap = 0;
  BR_FREE(arr->arr);
  arr->arr = NULL;
}

BR_API void points_groups_empty(points_groups_t* pg) {
  for (size_t i = 0; i < pg->len; ++i) {
    points_group_empty(&pg->arr[i]);
  }
}

void points_groups_add_test_points(points_groups_t* pg) {
  {
    int group = 0;
    points_group_t* g = points_group_get(pg, group);
    if (NULL == g) return;
    for (int i = 0; i < 10*1024; ++i)
      points_group_push_point(g, (Vector2){(float)g->len/128.f, sinf((float)g->len/128.f)});
  }
  {
    int group = 1;
    points_group_t* g = points_group_get(pg, group);
    if (NULL == g) return;
    for (int i = 0; i < 10*1024; ++i)
      points_group_push_point(g, (Vector2){-(float)g->len/128.f, sinf((float)g->len/128.f)});
  }
  {
    int group = 5;
    points_group_t* g = points_group_get(pg, group);
    if (NULL == g) return;
    for(int i = 0; i < 1024*1024; ++i) {
      float t = (float)(1 + g->len)*.1f;
      float x = sqrtf(t)*cosf(log2f(t));
      float y = sqrtf(t)*sinf(log2f(t));
      Vector2 p = {x, y};
      points_group_push_point(g, p);
    }
  }
  {
    points_group_t* g = points_group_get(pg, 6);
    if (NULL == g) return;
    int l = (int)g->len;
    for(int i = 0; i < 0; ++i) {
      for(int j = 0; j < 0; ++j) {
        int x = -50 + j + l;
        int y = (-50 + (i - j) + l) * (i % 2 == 0 ? 1 : -1);
        Vector2 p = {(float)x, (float)y};
        points_group_push_point(g, p);
      }
    }
  }
}

// Custom Blend Modes
#define GL_SRC_ALPHA 0x0302
#define GL_DST_ALPHA 0x0304
#define GL_MAX 0x8008

void points_groups_draw(points_groups_t pg, br_plot_instance_t* plot) {
  if (plot->kind == br_plot_instance_kind_2d) {
//    br_shader_line_uvs_t uvs = plot->dd.line_shader->uvs;
//    Vector2 size = {  uvs.zoom_uv.x * .01f, uvs.zoom_uv.y * .01f };
    rlSetBlendFactors(GL_SRC_ALPHA, GL_DST_ALPHA, GL_MAX);
    rlSetBlendMode(BLEND_CUSTOM);
    for (int j = 0; j < plot->groups_to_show.len; ++j) {
      size_t group = plot->groups_to_show.arr[j];
      points_group_t const* g = &pg.arr[group];
      if (g->len == 0) continue;
      if (g->is_selected) {
        resampling2_draw(g->resampling, g, plot);
      }
//      if (pgdi.show_closest) {
//        smol_mesh_gen_point1(pgdi.line_mesh, g->point_closest_to_mouse.graph_point, size, WHITE);
//      }
//      if (pgdi.show_x_closest) {
//        smol_mesh_gen_point1(pgdi.line_mesh, g->point_closest_to_mouse.graph_point_x, size, WHITE);
//      }
//      if (pgdi.show_y_closest) {
//        smol_mesh_gen_point1(pgdi.line_mesh, g->point_closest_to_mouse.graph_point_y, size, WHITE);
//      }
    }
    if (plot->dd.line_shader->len > 0) {
      br_shader_line_draw(plot->dd.line_shader);
      plot->dd.line_shader->len = 0;
    }
    rlSetBlendMode(BLEND_ALPHA);
  } else {
    assert(false);
    // TODO
  }
}

static points_group_t* points_group_init(points_group_t* g, int group_id) {
  *g = (points_group_t) { .cap = 1024, .len = 0, .group_id = group_id,
    .is_selected = true,
    .points = BR_MALLOC(sizeof(Vector2) * 1024),
    .resampling = resampling2_malloc(),
    .color = color_get(group_id),
    .name = br_str_malloc(32)
  };
  if (NULL != g->name.str) {
    sprintf(g->name.str, "Plot #%d", group_id);
    g->name.len = (unsigned int)strlen(g->name.str);
  }
  return g;
}

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
static Color base_colors[8] = { RED, GREEN, BLUE, LIGHTGRAY, PINK, GOLD, VIOLET, DARKPURPLE };
#pragma GCC diagnostic pop

static Color color_get(int id) {
  id = abs(id);
  static int base_colors_count = sizeof(base_colors)/sizeof(Color);
  float count = 2.f;
  Color c = base_colors[id%base_colors_count];
  id /= base_colors_count;
  while (id > 0) {
    c.r = (unsigned char)(((float)c.r + (float)base_colors[id%base_colors_count].r) / count);
    c.g = (unsigned char)(((float)c.g + (float)base_colors[id%base_colors_count].g) / count);
    c.b = (unsigned char)(((float)c.b + (float)base_colors[id%base_colors_count].b) / count);
    id /= base_colors_count;
    count += 1;
  }
  return c;
}

BR_API points_group_t* points_group_get(points_groups_t* pg, int group) {
  assert(pg);

  // TODO: da
  if (pg->len == 0) {
    pg->arr = BR_MALLOC(sizeof(points_group_t));
    if (NULL == pg->arr) return NULL;
    points_group_t* ret = points_group_init(&pg->arr[0], group);
    if (ret->points == NULL) return NULL;
    pg->cap = pg->len = 1;
    return ret;
  }

  for (size_t i = 0; i < pg->len; ++i) {
    if (pg->arr[i].group_id == group) {
      return &pg->arr[i];
    }
  }

  if (pg->len >= pg->cap) {
    size_t new_cap = pg->cap * 2;
    points_group_t* new_arr = BR_REALLOC(pg->arr, sizeof(points_group_t)*new_cap);
    if (NULL == new_arr) return NULL;
    pg->arr = new_arr;
    pg->cap = new_cap;
  }
  points_group_t* ret = points_group_init(&pg->arr[pg->len++], group);
  if (ret->points == NULL) {
    --pg->len;
    return NULL;
  }
  return ret;
}

BR_API void points_group_set_name(points_groups_t* pg, int group, br_str_t name) {
  points_group_t* g = points_group_get(pg, group);
  if (pg == NULL) return;
  br_str_free(g->name);
  g->name = name;
}

static void points_group_push_point(points_group_t* g, Vector2 v) {
  if (g->len >= g->cap && false == points_group_realloc(g, g->cap * 2)) return;
  if (g->len == 0) g->bounding_box = (bb_t) { v.x, v.y, v.x, v.y };
  else             br_bb_expand_with_point(&g->bounding_box, v);
  g->points[g->len] = v;
  resampling2_add_point(g->resampling, g, (uint32_t)g->len);
  ++g->len;
}

static void points_group_deinit(points_group_t* g) {
  // Free points
  BR_FREE(g->points);
  resampling2_free(g->resampling);
  br_str_free(g->name);
  g->points = NULL;
  g->len = g->cap = 0;
}

static bool points_group_realloc(points_group_t* pg, size_t new_cap) {
  Vector2* new_arr = BR_REALLOC(pg->points, new_cap * sizeof(Vector2));
  if (new_arr == NULL) {
    LOG("Out of memory. Can't add any more lines. Buy more RAM, or close Chrome\n");
    return false;
  }
  pg->points = new_arr;
  pg->cap = new_cap;
  return true;
}

static void br_bb_expand_with_point(bb_t* bb, Vector2 v) {
  bb->xmax = fmaxf(bb->xmax, v.x);
  bb->xmin = fminf(bb->xmin, v.x);
  bb->ymax = fmaxf(bb->ymax, v.y);
  bb->ymin = fminf(bb->ymin, v.y);
}

