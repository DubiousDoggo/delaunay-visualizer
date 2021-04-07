#include <SDL2/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstring>
#include <deque>
#include <iostream>
#include <random>
#include <string>
#include <tuple>
#include <unordered_set>
#include <vector>

#include "quad_edge.hh"

using namespace std::string_literals;

const int DEFAULT_WINDOW_WIDTH = 600;
const int DEFAULT_WINDOW_HEIGHT = 600;
const int GENERATE_POINTS = 50;

void cleanup(SDL_Window *&window, SDL_Renderer *&renderer) {
    SDL_DestroyRenderer(renderer);
    renderer = nullptr;
    SDL_DestroyWindow(window);
    window = nullptr;
    SDL_Quit();
}

struct vertex {
    int x, y;
    int vx, vy; // hacked on for velocity effect
    bool operator==(const vertex &rhs) const { return x == rhs.x && y == rhs.y; }
    bool operator<(const vertex &rhs) const { return x != rhs.x ? x < rhs.x : y < rhs.y; }
    friend std::ostream &operator<<(std::ostream &lhs, const vertex &rhs) { return lhs << '(' << rhs.x << ", " << rhs.y << ')'; }
};

namespace std {
template <> struct hash<vertex> {
    std::size_t operator()(const vertex &v) const noexcept { return ((std::size_t)v.x << 32) | (v.y); }
};
} // namespace std

using edge_t = edge_reference<const vertex *>;

/* true if and only if point D is interior to the region of the plane that is
 * bounded by the oriented circle ABC and lies to the left of it.
 * equivalent to:
 * | a.x  a.y  a.x^2 + a.y^2  1 |
 * | b.x  b.y  b.x^2 + b.y^2  1 |
 * | c.x  c.y  c.x^2 + c.y^2  1 |
 * | d.x  d.y  d.x^2 + d.y^2  1 |
 * =
 *                 | b.x  b.y  1 |
 * + a.x^2 + a.y^2 | c.x  c.y  1 |
 *                 | d.x  d.y  1 |
 *
 *                 | a.x  a.y  1 |
 * - b.x^2 + b.y^2 | c.x  c.y  1 |
 *                 | d.x  d.y  1 |
 *
 *                 | a.x  a.y  1 |
 * + c.x^2 + c.y^2 | b.x  b.y  1 |
 *                 | d.x  d.y  1 |
 *
 *                 | a.x  a.y  1 |
 * - d.x^2 + d.y^2 | b.x  b.y  1 |
 *                 | c.x  c.y  1 |
 * > 0
 */
bool in_circle(vertex a, vertex b, vertex c, vertex d) {
    // TODO optimize math prevent overflow
    return (((int64_t)a.x * a.x + a.y * a.y) * ((b.x * c.y - b.y * c.x) - (b.x * d.y - b.y * d.x) + (c.x * d.y - c.y * d.x)) -
            ((int64_t)b.x * b.x + b.y * b.y) * ((a.x * c.y - a.y * c.x) - (a.x * d.y - a.y * d.x) + (c.x * d.y - c.y * d.x)) +
            ((int64_t)c.x * c.x + c.y * c.y) * ((a.x * b.y - a.y * b.x) - (a.x * d.y - a.y * d.x) + (b.x * d.y - b.y * d.x)) -
            ((int64_t)d.x * d.x + d.y * d.y) * ((a.x * b.y - a.y * b.x) - (a.x * c.y - a.y * c.x) + (b.x * c.y - b.y * c.x))) > 0;
}

// true if the triangle a b c is oriented counter-clockwise.
// equivalent to:
// | a.x  a.y  1 |
// | b.x  b.y  1 | > 0
// | c.x  c.y  1 |
bool ccw(vertex a, vertex b, vertex c) { return ((a.x * b.y - a.y * b.x) - (a.x * c.y - a.y * c.x) + (b.x * c.y - b.y * c.x)) > 0; }
bool right_of(vertex x, edge_t e) { return ccw(x, *e.DEST, *e.ORG); }
bool left_of(vertex x, edge_t e) { return ccw(x, *e.ORG, *e.DEST); }
bool valid(edge_t e, edge_t base) { return right_of(*e.DEST, base); }

SDL_Window *window = nullptr;
SDL_Renderer *window_renderer = nullptr;
SDL_Texture *graph_texture = nullptr;

void draw_graph(SDL_Renderer *renderer, edge_t l, edge_t r) {

    std::deque<edge_t> edge_queue;
    std::unordered_set<const vertex *> checked_verts;
    edge_queue.push_back(l);
    checked_verts.insert(l.ORG);
    edge_queue.push_back(r);
    checked_verts.insert(r.ORG);

    while (!edge_queue.empty()) {
        edge_t p = edge_queue.front();
        edge_queue.pop_front();
        edge_t e = p;
        do {
            if (checked_verts.insert(e.DEST).second)
                edge_queue.push_back(e.sym());
            SDL_RenderDrawLine(renderer, e.ORG->x, e.ORG->y, e.DEST->x, e.DEST->y);
            e = e.o_next();
        } while (e != p);
    }
}

void do_render_step(edge_t l, edge_t r, const vertex &begin, const vertex &end) {
    SDL_Surface *surface;
    SDL_LockTextureToSurface(graph_texture, NULL, &surface);
    SDL_Renderer *surface_renderer = SDL_CreateSoftwareRenderer(surface);

    const int bbox_oversize = 3;
    SDL_Rect bbox = {begin.x - bbox_oversize, 0, end.x - begin.x + 1 + 2 * bbox_oversize, surface->h};
    SDL_SetRenderDrawColor(surface_renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderFillRect(surface_renderer, &bbox);

    SDL_SetRenderDrawColor(surface_renderer, 0x00, 0xFF, 0xFF, SDL_ALPHA_OPAQUE);
    SDL_RenderDrawRect(surface_renderer, &bbox);

    SDL_SetRenderDrawColor(surface_renderer, 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE);
    draw_graph(surface_renderer, l, r);

    SDL_DestroyRenderer(surface_renderer);
    SDL_UnlockTexture(graph_texture);

    SDL_SetRenderDrawColor(window_renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(window_renderer);
    SDL_RenderCopyEx(window_renderer, graph_texture, NULL, NULL, 0, 0, SDL_FLIP_VERTICAL);
    SDL_RenderPresent(window_renderer);

    bool cont = false;
    SDL_Event event;
    std::cout << "waiting\n";
    while (!cont) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                std::cout << "quit event in render step" << std::endl;
            if (event.type == SDL_KEYDOWN)
                if (event.key.keysym.sym == SDLK_SPACE)
                    cont = true;
        }
    }
}

#define PRINT_EDGE(e) e << " ( " << *e.ORG << " -> " << *e.DEST << " )"
#define DEBUG(p) p
#define RENDER_STEP_ENABLE
#ifdef RENDER_STEP_ENABLE
#define RENDER_STEP(l, r) do_render_step(l, r, begin[0], end[-1])
#else
#define RENDER_STEP(l, r)
#endif

std::pair<edge_t, edge_t> delaunay(std::vector<vertex>::const_iterator begin, std::vector<vertex>::const_iterator end) {
    DEBUG(std::cout << "->   delaunay( " << *begin << ", " << end[-1] << " )\n";)
    if (end - begin == 2) {
        // create an edge from s1 to s2
        edge_t a = make_edge<const vertex *>();
        a.ORG = &begin[0];
        a.DEST = &begin[1];
        DEBUG(std::cout << "make_edge " << PRINT_EDGE(a) << "\n"
                        << "<-1  delaunay( " << *begin << ", " << end[-1] << " ) : [ " << a << ", " << a.sym() << " ]\n";)
        RENDER_STEP(a, a.sym());
        return {a, a.sym()};
    } else if (end - begin == 3) {
        // create triangle
        const vertex &s1 = begin[0];
        const vertex &s2 = begin[1];
        const vertex &s3 = begin[2];

        edge_t a = make_edge<const vertex *>();
        edge_t b = make_edge<const vertex *>();
        splice(a.sym(), b);
        a.ORG = &s1;
        a.DEST = &s2;
        b.ORG = &s2;
        b.DEST = &s3;

        RENDER_STEP(a, b);

        if (ccw(s1, s2, s3)) {
            // edge_t c =
            connect(b, a);
            RENDER_STEP(a, b);
            DEBUG(std::cout << "<-2a delaunay( " << *begin << ", " << end[-1] << " ) : [ " << a << ", " << b.sym() << " ]\n";)
            return {a, b.sym()};
        } else if (ccw(s1, s3, s2)) {
            edge_t c = connect(b, a);
            RENDER_STEP(a, b);
            DEBUG(std::cout << "<-2b delaunay( " << *begin << ", " << end[-1] << " ) : [ " << c.sym() << ", " << c.sym() << " ]\n";)
            return {c.sym(), c};
        } else { // points are colinear
            DEBUG(std::cout << "<-2c delaunay( " << *begin << ", " << end[-1] << " ) : [ " << a << ", " << b.sym() << " ]\n";)
            return {a, b.sym()};
        }
    } else {
        auto mid = begin + (end - begin) / 2;
        auto [ldo, ldi] = delaunay(begin, mid);
        auto [rdi, rdo] = delaunay(mid, end);
        RENDER_STEP(ldo, rdo);

        DEBUG(std::cout << "--   delaunay( " << *begin << ", " << end[-1] << " )\n"
                        << "ldi " << PRINT_EDGE(ldi) << "\nrdi " << PRINT_EDGE(rdi) << "\n";)

        while (true) { // find lower common tangent of L and R
            if (left_of(*rdi.ORG, ldi))
                ldi = ldi.l_next();
            else if (right_of(*ldi.ORG, rdi))
                rdi = rdi.r_prev();
            else
                break;
        }
        DEBUG(std::cout << "found base\n"
                        << "ldi " << PRINT_EDGE(ldi) << "\nrdi " << PRINT_EDGE(rdi) << "\n";)
        edge_t base_l = connect(rdi.sym(), ldi); // create base RL edge
        DEBUG(std::cout << "connect base " << PRINT_EDGE(base_l) << "\n";)
        RENDER_STEP(ldi, rdi);

        if (ldi.ORG == ldo.ORG)
            ldo = base_l.sym();
        if (rdi.ORG == rdo.ORG)
            rdo = base_l;

        while (true) { // merge loop
            edge_t l_cand = base_l.sym().o_next();
            if (valid(l_cand, base_l)) {
                while (in_circle(*base_l.DEST, *base_l.ORG, *l_cand.DEST, *l_cand.o_next().DEST)) {
                    edge_t t = l_cand.o_next();
                    DEBUG(std::cout << "delete L cand" << PRINT_EDGE(l_cand) << "\n";)
                    delete_edge(l_cand);
                    l_cand = t;
                    RENDER_STEP(ldo, rdo);
                }
            }
            edge_t r_cand = base_l.o_prev();
            if (valid(r_cand, base_l)) {
                while (in_circle(*base_l.DEST, *base_l.ORG, *r_cand.DEST, *r_cand.o_prev().DEST)) {
                    edge_t t = r_cand.o_prev();
                    DEBUG(std::cout << "delete R cand " << PRINT_EDGE(r_cand) << "\n";)
                    delete_edge(r_cand);
                    r_cand = t;
                    RENDER_STEP(ldo, rdo);
                }
            }
            if (!valid(l_cand, base_l) && !valid(r_cand, base_l)) {
                DEBUG(std::cout << "L & R cand invalid, break\n";)
                break;
            }
            if (!valid(l_cand, base_l) || (valid(r_cand, base_l) && in_circle(*l_cand.DEST, *l_cand.ORG, *r_cand.ORG, *r_cand.DEST))) {
                base_l = connect(r_cand, base_l.sym());
                DEBUG(std::cout << "connect R cand " << PRINT_EDGE(base_l) << "\n";)
                RENDER_STEP(ldo, rdo);
            } else {
                base_l = connect(base_l.sym(), l_cand.sym());
                DEBUG(std::cout << "connect L cand " << PRINT_EDGE(base_l) << "\n";)
                RENDER_STEP(ldo, rdo);
            }
        }
        DEBUG(std::cout << "<-3  delaunay( " << *begin << ", " << end[-1] << " ) : [ " << ldo << ", " << rdo << "]\n";)
        RENDER_STEP(ldo, rdo);
        return {ldo, rdo};
    }
}

const long long seed = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
std::mt19937 random(seed);
std::uniform_real_distribution real;

int main(int argc, char **argv) {

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        std::cout << "Can't initialize SDL: " << SDL_GetError() << std::endl;
        cleanup(window, window_renderer);
        return EXIT_FAILURE;
    }
    window = SDL_CreateWindow(("delaunay triangulator number "s + std::to_string(random())).c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              DEFAULT_WINDOW_WIDTH, DEFAULT_WINDOW_HEIGHT, SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window == NULL) {
        std::cout << "Can't create window: " << SDL_GetError() << std::endl;
        cleanup(window, window_renderer);
        return EXIT_FAILURE;
    }
    window_renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (window_renderer == NULL) {
        std::cout << "Can't create renderer: " << SDL_GetError() << std::endl;
        cleanup(window, window_renderer);
        return EXIT_FAILURE;
    }
    SDL_RendererInfo info;
    if (SDL_GetRendererInfo(window_renderer, &info) != 0) {
        std::cout << "renderer info failed: " << SDL_GetError() << std::endl;
        cleanup(window, window_renderer);
        return EXIT_FAILURE;
    }
    std::printf("renderer info\n\tname: %s\n\tflags: %d\n\ttexture formats (%u):\n", info.name, info.flags, info.num_texture_formats);
    for (size_t i = 0; i < info.num_texture_formats; i++)
        std::printf("\t\t%s\n", SDL_GetPixelFormatName(info.texture_formats[i]));
    std::printf("\tmax texture size: %d x %d\n", info.max_texture_width, info.max_texture_height);

    const int point_range = DEFAULT_WINDOW_HEIGHT;
    graph_texture = SDL_CreateTexture(window_renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, point_range, point_range);
    SDL_Surface *surface;

    // generate points
    // TODO make sure there are no duplicate points
    std::vector<vertex> points;
    std::uniform_int_distribution range(0, point_range - 1);
    for (int i = 0; i < GENERATE_POINTS; i++)
        points.push_back({range(random), range(random)});

    // test case from Samuel Peterson
    // http://www.geom.uiuc.edu/~samuelp/del_project.html
    // const int demo_scale = point_range / 7;
    // std::vector<vertex> points = {{0, 1}, {1, 0}, {1, 2}, {1, 3}, {2, 1}, {3, 3}, {4, 2}, {5, 0}, {5, 1}, {5, 3}};
    // for (vertex &v : points) {
    //     v.x = (v.x + 1) * demo_scale;
    //     v.y = (v.y + 1) * demo_scale;
    // }

    // compute triangulation
    std::sort(points.begin(), points.end());
    auto [l, r] = delaunay(points.begin(), points.end());
    std::cout << "finished\n";

    SDL_LockTextureToSurface(graph_texture, NULL, &surface);
    SDL_Renderer *surface_renderer = SDL_CreateSoftwareRenderer(surface);
    SDL_SetRenderDrawColor(surface_renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(surface_renderer);

    // draw graph
    SDL_SetRenderDrawColor(surface_renderer, 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE);
    draw_graph(surface_renderer, l, r);

    // draw points
    SDL_SetRenderDrawColor(surface_renderer, 0xFF, 0x00, 0x00, SDL_ALPHA_OPAQUE);
    for (const vertex &v : points)
        SDL_RenderDrawPoint(surface_renderer, v.x, v.y);

    SDL_DestroyRenderer(surface_renderer);
    SDL_UnlockTexture(graph_texture);

    kill_graph(l);

    // initialize velocity effect
    std::uniform_int_distribution vel_range{-2, 2};
    for (vertex &v : points) {
        v.vx = vel_range(random);
        v.vy = vel_range(random);
    }

    bool running = true;
    while (running) {

        SDL_SetRenderDrawColor(window_renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
        SDL_RenderClear(window_renderer);
        SDL_RenderCopyEx(window_renderer, graph_texture, NULL, NULL, 0, 0, SDL_FLIP_VERTICAL);
        SDL_RenderPresent(window_renderer);

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT)
                running = false;
#ifndef RENDER_STEP_ENABLE
            if (event.type == SDL_KEYDOWN) {
                SDL_LockTextureToSurface(graph_texture, NULL, &surface);
                SDL_Renderer *surface_renderer = SDL_CreateSoftwareRenderer(surface);
                SDL_SetRenderDrawColor(surface_renderer, 0x00, 0x00, 0x00, SDL_ALPHA_OPAQUE);
                SDL_RenderClear(surface_renderer);
                switch (event.key.keysym.sym) {
                    case SDLK_q: { // wiggle
                        for (vertex &v : points) {
                            v.x = modulo(v.x + std::uniform_int_distribution{-5, 5}(random), point_range);
                            v.y = modulo(v.y + std::uniform_int_distribution{-5, 5}(random), point_range);
                        }
                    } break;
                    case SDLK_w: { // move up
                        for (vertex &v : points)
                            v.y = modulo(v.y + 1, point_range);
                    } break;
                    case SDLK_a: { // move left
                        for (vertex &v : points)
                            v.x = modulo(v.x - 1, point_range);
                    } break;
                    case SDLK_s: { // move down
                        for (vertex &v : points)
                            v.y = modulo(v.y - 1, point_range);
                    } break;
                    case SDLK_d: { // move right
                        for (vertex &v : points)
                            v.x = modulo(v.x + 1, point_range);
                    } break;
                    case SDLK_e: { // velocity effect
                        for (vertex &v : points) {
                            if (v.x + v.vx < 0 || v.x + v.vx >= point_range)
                                v.vx = -v.vx;
                            if (v.y + v.vy < 0 || v.y + v.vy >= point_range)
                                v.vy = -v.vy;
                            v.x += v.vx;
                            v.y += v.vy;
                        }
                    } break;
                    case SDLK_x: points.push_back({range(random), range(random)}); break;
                    case SDLK_z: points.pop_back(); break;
                }
                // recompute triangulation
                std::sort(points.begin(), points.end());
                auto [l, r] = delaunay(points.begin(), points.end());

                // draw graph
                SDL_SetRenderDrawColor(surface_renderer, 0xFF, 0xFF, 0xFF, SDL_ALPHA_OPAQUE);
                draw_graph(surface_renderer, l, r);

                // draw points
                SDL_SetRenderDrawColor(surface_renderer, 0xFF, 0x00, 0x00, SDL_ALPHA_OPAQUE);
                for (const vertex &v : points)
                    SDL_RenderDrawPoint(surface_renderer, v.x, v.y);

                SDL_DestroyRenderer(surface_renderer);
                SDL_UnlockTexture(graph_texture);

                kill_graph(l);
            }
#endif
        }
    }

    cleanup(window, window_renderer);
    return 0;
}