#include "br_plot.h"
#include "br_gui_internal.h"
#include "br_help.h"
#include "br_da.h"

#include <assert.h>
#include <math.h>
#include <string.h>

#include "raylib.h"
#include "raymath.h"
#include "rlgl.h"

context_t context;

void emscripten_run_script(const char* script);

BR_API br_plotter_t* br_plotter_malloc(void) {
  return BR_MALLOC(sizeof(br_plotter_t));
}

BR_API void br_plotter_init(br_plotter_t* br, float width, float height) {
  SetConfigFlags(FLAG_MSAA_4X_HINT);
  InitWindow((int)width, (int)height, "brplot");
  SetWindowState(FLAG_VSYNC_HINT | FLAG_MSAA_4X_HINT | FLAG_WINDOW_RESIZABLE);
  *br = (br_plotter_t){
    .groups = {0},
    .groups_3d = {0},
    .plots = {0},
    .shaders = {0},
    .commands = {0},
    .shaders_dirty = false,
  };
#ifdef IMGUI
#ifndef RELEASE
#ifdef LINUX
  br->hot_state = (br_hotreload_state_t) { .handl = NULL, .func_loop = NULL, .func_init = NULL, .is_init_called = false, .lock = { 0 } };
  pthread_mutexattr_t attrs;
  pthread_mutexattr_init(&attrs);
  pthread_mutex_init(&br->hot_state.lock, &attrs);
#endif
#endif
#endif
  br->shaders = br_shaders_malloc();
  q_init(&br->commands);
  help_load_default_font();

  context.font_scale = 1.8f;
  memset(context.buff, 0, sizeof(context.buff));
  br_gui_init_specifics_gui(br);
  br_gui_init_specifics_platform(br);
}

BR_API void br_plotter_resize(br_plotter_t* br, float width, float height) {
  (void)br;
  SetWindowSize((int)width, (int)height);
}

BR_API points_groups_t* br_plotter_get_points_groups(br_plotter_t* br) {
  return &br->groups;
}

void br_plotter_add_plot_instance_2d(br_plotter_t* br) {
  br_plot_instance_t plot = {
    .groups_to_show = { 0 },
    .graph_screen_rect = { GRAPH_LEFT_PAD, 50, (float)GetScreenWidth() - GRAPH_LEFT_PAD - 60, (float)GetScreenHeight() - 110 },
    .resolution = { (float)GetScreenWidth(), (float)GetScreenHeight() },
    .follow = false,
    .jump_around = false,
    .mouse_inside_graph = false,
    .kind = br_plot_instance_kind_2d,
    .dd =  {
      .line_shader = br->shaders.line,
      .grid_shader = br->shaders.grid,
      .zoom = { 1.f, 1.f },
      .offset = { 0.f, 0.f },
      .delta = { 0 },
    }
  };
  br_da_push_t(int, (br->plots), plot);
}

void br_plotter_add_plot_instance_3d(br_plotter_t* br) {
  br_plot_instance_t plot = {
    .groups_to_show = { 0 },
    .graph_screen_rect = { GRAPH_LEFT_PAD, 50, (float)GetScreenWidth() - GRAPH_LEFT_PAD - 60, (float)GetScreenHeight() - 110 },
    .resolution = { (float)GetScreenWidth(), (float)GetScreenHeight() },
    .follow = false,
    .jump_around = false,
    .mouse_inside_graph = false,
    .kind = br_plot_instance_kind_3d,
    .ddd =  {
      .grid_shader = br->shaders.grid_3d,
      .line_shader = br->shaders.line_3d,
      .line_simple_shader = br->shaders.line_3d_simple,
      .eye = { 0, 0, 100 },
      .target = { 0, 0, 0},
      .up = { 0, 1, 0},
      .fov_y = 1,
      .near_plane = 0.001f,
      .far_plane = 10e7f,
      .groups_3d_to_show = { 0 }
    }
  };
  br_da_push_t(int, (br->plots), plot);
}

BR_API void br_plotter_free(br_plotter_t* gv) {
  br_shaders_free(gv->shaders);
  for (size_t i = 0; i < gv->groups.len; ++i) {
    points_groups_deinit(&gv->groups);
  }
  q_command c = q_pop(&gv->commands);
  while(c.type != q_command_none) {
    if (c.type == q_command_set_name) br_str_free(c.set_quoted_str.str);
    c = q_pop(&gv->commands);
  }
  BR_FREE(gv->commands.commands);
  BR_FREE(gv->plots.arr);
}

bool br_plot_instance_update_variables_2d(br_plot_instance_t* plot, points_groups_t const groups, Vector2 mouse_pos) {
  assert(plot->kind == br_plot_instance_kind_2d);
  if (plot->follow) {
    Rectangle sr = plot->dd.graph_rect;
    Vector2 middle = { sr.x + sr.width/2, sr.y - sr.height/2 };
    for (size_t i = 0; i < groups.len; ++i) {
      points_group_t* pg = &groups.arr[i];
      size_t gl = pg->len;
      if (!pg->is_selected || gl == 0) continue;
      plot->dd.delta.x += ((middle.x - pg->points[gl - 1].x))/1000.f;
      plot->dd.delta.y += ((middle.y - pg->points[gl - 1].y))/1000.f;
    }
    plot->dd.offset.x -= plot->dd.delta.x;
    plot->dd.offset.y -= plot->dd.delta.y;
    plot->dd.delta.x *= plot->dd.recoil;
    plot->dd.delta.y *= plot->dd.recoil;
  } else {
    plot->dd.delta = (Vector2){ 0.f, 0.f };
  }
  if (plot->mouse_inside_graph) {
    // TODO: Move this to br_keybindings.c
    // Stuff related to zoom
    {
      float mw = -GetMouseWheelMove();
      Vector2 old = plot->dd.mouse_pos;
      bool any = false;
      if (false == help_near_zero(mw)) {
        float mw_scale = (1 + mw/10);
        if (IsKeyDown(KEY_X)) {
          plot->dd.zoom.x *= mw_scale;
        } else if (IsKeyDown(KEY_Y)) {
          plot->dd.zoom.y *= mw_scale;
        } else {
          plot->dd.zoom.x *= mw_scale;
          plot->dd.zoom.y *= mw_scale;
        }
        any = true;
      }
      if (IsKeyDown(KEY_X) && IsKeyDown(KEY_LEFT_SHIFT)) any = true,   plot->dd.zoom.x *= 1.1f;
      if (IsKeyDown(KEY_Y) && IsKeyDown(KEY_LEFT_SHIFT)) any = true,   plot->dd.zoom.y *= 1.1f;
      if (IsKeyDown(KEY_X) && IsKeyDown(KEY_LEFT_CONTROL)) any = true, plot->dd.zoom.x *= .9f;
      if (IsKeyDown(KEY_Y) && IsKeyDown(KEY_LEFT_CONTROL)) any = true, plot->dd.zoom.y *= .9f;
      if (any) {
        br_plot_instance_update_context(plot, mouse_pos);
        Vector2 now = plot->dd.mouse_pos;
        plot->dd.offset.x -= now.x - old.x;
        plot->dd.offset.y -= now.y - old.y;
      }
    }
    if (plot->jump_around) {
      plot->graph_screen_rect.x += 100.f * (float)sin(GetTime());
      plot->graph_screen_rect.y += 77.f * (float)cos(GetTime());
      plot->graph_screen_rect.width += 130.f * (float)sin(GetTime());
      plot->graph_screen_rect.height += 177.f * (float)cos(GetTime());
    }
    if (IsMouseButtonDown(MOUSE_RIGHT_BUTTON)) {
      Vector2 delt = GetMouseDelta();
      //float speed = 1.f;
      if (IsKeyDown(KEY_W)) {
        //Vector3 diff = Vector3Subtract(br->eye, br->target);
      }
      plot->dd.offset.x -= plot->dd.zoom.x*delt.x/plot->graph_screen_rect.height;
      plot->dd.offset.y += plot->dd.zoom.y*delt.y/plot->graph_screen_rect.height;
      return false;
    } else return true;
  }
  return false;
}

bool br_plot_instance_update_variables_3d(br_plot_instance_t* plot, points_groups_t const groups, Vector2 mouse_pos) {
  assert(plot->kind == br_plot_instance_kind_3d);
  if (!plot->mouse_inside_graph) return false;
  if (IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
    Vector2 md = Vector2Scale(GetMouseDelta(), -0.003f);
    Vector3 zeroed = Vector3Subtract(plot->ddd.eye, plot->ddd.target);
    Vector3 rotated_up = Vector3RotateByAxisAngle(zeroed, plot->ddd.up, md.x);
    Vector3 right = Vector3CrossProduct(plot->ddd.up, Vector3Normalize(zeroed));
    Vector3 rotated_right = Vector3RotateByAxisAngle(rotated_up, right, md.y);
    if (fabsf(Vector3DotProduct(rotated_right, plot->ddd.up)) > 0.94f) plot->ddd.eye = Vector3Add(rotated_up,    plot->ddd.target);
    else                                                               plot->ddd.eye = Vector3Add(rotated_right, plot->ddd.target);
    plot->ddd.eye = Vector3Add(rotated_right, plot->ddd.target);
    return false;
  }
  {
    float mw = GetMouseWheelMove();
    float mw_scale = (1 + mw/10);
    Vector3 zeroed = Vector3Subtract(plot->ddd.eye, plot->ddd.target);
    float len = Vector3Length(zeroed);
    len *= mw_scale;
    plot->ddd.eye = Vector3Add(Vector3Scale(Vector3Normalize(zeroed), len), plot->ddd.target);
  }
  return true;
}

void br_plotter_update_variables(br_plotter_t* br) {
#ifndef RELEASE
  if (br->shaders_dirty) {
    br_shaders_refresh(br->shaders);
    br->shaders_dirty = false;
  }
#endif
  Vector2 mouse_pos = GetMousePosition();
  br_plotter_update_context(br, mouse_pos);
  for (int i = 0; i < br->plots.len; ++i) {
    br_plot_instance_t* plot = &br->plots.arr[i];
    switch (plot->kind) {
      case br_plot_instance_kind_2d: {
        if (br_plot_instance_update_variables_2d(plot, br->groups, mouse_pos))
          br_keybinding_handle_keys(br, plot);
      } break;
      case br_plot_instance_kind_3d: {
        if (br_plot_instance_update_variables_3d(plot, br->groups, mouse_pos))
          br_keybinding_handle_keys(br, plot);
      } break;
      default: assert(0);
    }
  }

  while (1) {
    q_command comm = q_pop(&br->commands);
    switch (comm.type) {
      case q_command_none:          goto end;
      case q_command_push_point_x:  points_group_push_x(&br->groups, comm.push_point_x.x, comm.push_point_y.group); break;
      case q_command_push_point_y:  points_group_push_y(&br->groups, comm.push_point_y.y, comm.push_point_y.group); break;
      case q_command_push_point_xy: points_group_push_xy(&br->groups, comm.push_point_xy.x, comm.push_point_xy.y, comm.push_point_xy.group); break;
      case q_command_pop:           break; //TODO
      case q_command_clear:         points_group_clear(&br->groups, comm.clear.group); break;
      case q_command_clear_all:     points_groups_deinit(&br->groups); break;
      case q_command_screenshot:    br_plot_instance_screenshot(&br->plots.arr[0], br->groups, comm.path_arg.path); free(comm.path_arg.path); break;
      case q_command_export:        br_plotter_export(br, comm.path_arg.path);     free(comm.path_arg.path); break;
      case q_command_exportcsv:     br_plotter_export_csv(br, comm.path_arg.path); free(comm.path_arg.path); break;
      case q_command_hide:          points_group_get(&br->groups, comm.hide_show.group)->is_selected = false; break;
      case q_command_show:          points_group_get(&br->groups, comm.hide_show.group)->is_selected = true;  break;
      case q_command_set_name:      points_group_set_name(&br->groups, comm.set_quoted_str.group, comm.set_quoted_str.str);  break;
      case q_command_focus:         br_plotter_focus_visible(&br->plots.arr[0], br->groups); break;
      default:                      BR_ASSERT(false);
    }
  }
  end: return;
}
void br_plot_instance_update_shader_values(br_plot_instance_t* plot) {
  switch (plot->kind) {
    case br_plot_instance_kind_2d: {
      Vector2 zoom = plot->dd.zoom;
      Vector2 zoom_log = { .x = powf(10.f, -floorf(log10f(zoom.x))), .y = powf(10.f, -floorf(log10f(zoom.y))) };
      Vector2 zoom_final = { .x = zoom.x * zoom_log.x, .y = zoom.y * zoom_log.y };
      plot->dd.grid_shader->uvs.zoom_uv = zoom_final;
      Vector2 off_zoom = Vector2Multiply(plot->dd.offset, zoom_log);
      Vector2 off = Vector2Divide(off_zoom, (Vector2) { 10, 10 });
      plot->dd.grid_shader->uvs.offset_uv = Vector2Subtract(off_zoom, (Vector2) { floorf(off.x) * 10.f, floorf(off.y) * 10.f });

      plot->dd.grid_shader->uvs.screen_uv = (Vector2) { .x = plot->graph_screen_rect.width, .y = plot->graph_screen_rect.height };

      plot->dd.line_shader->uvs.zoom_uv = plot->dd.zoom;
      plot->dd.line_shader->uvs.offset_uv = plot->dd.offset;
      plot->dd.line_shader->uvs.screen_uv = plot->resolution;
      plot->dd.line_shader->uvs.resolution_uv = *(Vector4*)&plot->graph_screen_rect;
    } break;
    case br_plot_instance_kind_3d: {
      Vector2 re = plot->ddd.grid_shader->uvs.resolution_uv = (Vector2) { .x = plot->graph_screen_rect.width, .y = plot->graph_screen_rect.height };
      Matrix per = MatrixPerspective(plot->ddd.fov_y, re.x / re.y, plot->ddd.near_plane, plot->ddd.far_plane);
      Matrix look = MatrixLookAt(plot->ddd.eye, plot->ddd.target, plot->ddd.up);
      plot->ddd.grid_shader->uvs.m_mvp_uv = MatrixMultiply(look, per);
      plot->ddd.grid_shader->uvs.eye_uv = plot->ddd.eye;

      plot->ddd.line_shader->uvs.m_mvp_uv = MatrixMultiply(look, per);
      plot->ddd.line_shader->uvs.eye_uv = plot->ddd.eye;
    } break;
    default: assert(0);
  }
}

BR_API void br_plotter_frame_end(br_plotter_t* gv) {
  (void)gv;
  for (size_t i = 0; i < gv->groups.len; ++i) {
    if (gv->groups.arr[i].is_new == false) continue;
    for (int j = 0; j < gv->plots.len; ++j) {
      br_da_push_t(int, gv->plots.arr[j].groups_to_show, i);
    }
    gv->groups.arr[i].is_new = false;
  }
  //gv->lines_mesh->draw_calls = 0;
  //gv->lines_mesh->points_drawn = 0;
}

void br_plotter_export(br_plotter_t const* gv, char const * path) {
  FILE* file = fopen(path, "w");
  if (file == NULL) {
    fprintf(stderr, "Failed to open path: `%s`", path);
    return;
  }
// TODO
#if 0
  fprintf(file, "--zoomx %f\n", gv->uvZoom.x);
  fprintf(file, "--zoomy %f\n", gv->uvZoom.y);
  fprintf(file, "--offsetx %f\n", gv->uvOffset.x);
  fprintf(file, "--offsety %f\n", gv->uvOffset.y);
#endif
  for (size_t i = 0; i < gv->groups.len; ++i) {
    fprintf(file, gv->groups.arr[i].is_selected ? "--show %d\n" : "--hide %d\n", gv->groups.arr[i].group_id);
  }
  points_groups_export(&gv->groups, file);
  fclose(file);
}

void br_plotter_export_csv(br_plotter_t const* br, char const * path) {
  FILE* file = fopen(path, "w");
  // TODO: Show user an error message
  if (file == NULL) return;
  points_groups_export_csv(&br->groups, file);
  fclose(file);
}

void br_plot_instance_update_context(br_plot_instance_t* plot, Vector2 mouse_pos) {
  Vector2 mp_in_graph = { mouse_pos.x - plot->graph_screen_rect.x, mouse_pos.y - plot->graph_screen_rect.y };
  plot->mouse_inside_graph = CheckCollisionPointRec(mouse_pos, plot->graph_screen_rect);
  if (plot->kind == br_plot_instance_kind_2d) {
    plot->dd.mouse_pos = (Vector2) {
    -(plot->graph_screen_rect.width  - 2.f*mp_in_graph.x)/plot->graph_screen_rect.height*plot->dd.zoom.x/2.f + plot->dd.offset.x,
     (plot->graph_screen_rect.height - 2.f*mp_in_graph.y)/plot->graph_screen_rect.height*plot->dd.zoom.y/2.f + plot->dd.offset.y};
    plot->dd.graph_rect = (Rectangle){-plot->graph_screen_rect.width/plot->graph_screen_rect.height*plot->dd.zoom.x/2.f + plot->dd.offset.x,
      plot->dd.zoom.y/2.f + plot->dd.offset.y,
      plot->graph_screen_rect.width/plot->graph_screen_rect.height*plot->dd.zoom.x,
      plot->dd.zoom.y};
  } else {
    // TODO 2D/3D
    //assert(false);
  }
}

void br_plotter_update_context(br_plotter_t* br, Vector2 mouse_pos) {
// TODO 2D/3D
  for (int i = 0; i < br->plots.len; ++i) br_plot_instance_update_context(&br->plots.arr[i], mouse_pos);
}

void draw_grid_numbers(br_plot_instance_t* plot) {
  // TODO 2D/3D
  //assert(plot->kind == br_plot_instance_kind_2d);
  if(plot->kind != br_plot_instance_kind_2d) return;

  Rectangle r = plot->dd.graph_rect;
  Rectangle graph_screen_rect = plot->graph_screen_rect;
  float font_size = 15.f * context.font_scale;
  char fmt[16];

  float exp = floorf(log10f(r.height / 2.f));
  if (false == isnan(exp)) {
    float base = powf(10.f, exp);
    float start = floorf(r.y / base) * base;
    if (exp >= 0) strcpy(fmt, "%f");
    else sprintf(fmt, "%%.%df", -(int)exp);

    float i = 0.f;
    while (base * i < r.height) {
      float cur = start - base * i;
      i += 1.f;
      sprintf(context.buff, fmt, cur);
      help_trim_zeros(context.buff);
      Vector2 sz = help_measure_text(context.buff, font_size);
      float y = graph_screen_rect.y + (graph_screen_rect.height / r.height) * (r.y - cur);
      y -= sz.y / 2.f;
      help_draw_text(context.buff, (Vector2){ .x = graph_screen_rect.x - sz.x - 2.f, .y = y }, font_size, RAYWHITE);
    }
  }

  exp =  floorf(log10f(r.width / 2.f));
  if (false == isnan(exp)) {
    float base = powf(10.f, exp);
    if (isnan(base) || isinf(base)) return;
    float start = ceilf(r.x / base) * base;
    if (exp >= 0) strcpy(fmt, "%f");
    else sprintf(fmt, "%%.%df", -(int)exp);
    float x_last_max = -INFINITY;
    float i = 0;
    while (base * i < r.width) {
      float cur = start + base * i;
      i += 1.f;
      sprintf(context.buff, fmt, cur);
      help_trim_zeros(context.buff);
      Vector2 sz = help_measure_text(context.buff, font_size);
      float x = graph_screen_rect.x + (graph_screen_rect.width / r.width) * (cur - r.x);
      x -= sz.x / 2.f;
      if (x - 5.f < x_last_max) continue; // Don't print if it will overlap with the previous text. 5.f is padding.
      x_last_max = x + sz.x;
      help_draw_text(context.buff, (Vector2){ .x = x, .y = graph_screen_rect.y + graph_screen_rect.height }, font_size, RAYWHITE);
    }
  }
}

