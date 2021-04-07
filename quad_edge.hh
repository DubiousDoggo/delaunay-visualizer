
#include <algorithm>
#include <functional>
#include <ostream>
#include <vector>

inline unsigned modulo(int a, int b) {
    int m = a % b;
    if (m < 0)
        m = (b < 0) ? m - b : m + b;
    return m;
}

template <typename T> struct quad_edge;

template <typename T> struct edge_reference {
    quad_edge<T> *q; // parent edge group
    unsigned r;      // rotation with respect to canonincal edge
    T &data() { return q->e[r].data; }

    bool operator==(const edge_reference<T> &rhs) const { return q == rhs.q && r == rhs.r; }
    bool operator!=(const edge_reference<T> &rhs) const { return !(*this == rhs); }
    friend std::ostream &operator<<(std::ostream &lhs, const edge_reference<T> &rhs) { return lhs << rhs.q << '[' << rhs.r << ']'; }

    edge_reference<T> rot(int n = 1) const { return {q, modulo(r + n, 4)}; }
    edge_reference<T> sym() const { return rot(2); }

    edge_reference<T> o_next() const; // see below
    edge_reference<T> l_next() const { return rot(-1).o_next().rot(); }
    edge_reference<T> r_next() const { return rot().o_next().rot(-1); }
    edge_reference<T> d_next() const { return sym().o_next().sym(); }

    edge_reference<T> o_prev() const { return rot().o_next().rot(); }
    edge_reference<T> l_prev() const { return o_next().sym(); }
    edge_reference<T> r_prev() const { return sym().o_next(); }
    edge_reference<T> d_prev() const { return rot(-1).o_next().rot(-1); }
};

template <typename T> struct quarter_record {
    edge_reference<T> next;
    T data;
};

template <typename T> struct quad_edge { quarter_record<T> e[4]; };

template <typename T> edge_reference<T> edge_reference<T>::o_next() const { return q->e[r].next; }

// From Guibas & Stolfi:
// returns an edge e of a newly created data structure representing a
// subdivision of the sphere. Apart from orientation and direction, e will be
// the only edge of the subdivision and will not be a loop; we have
// e Org != e Dest, eLeft = e Right, e Lnext = e Rnext = e Sym, and
// e Onext = e Oprev = e. To construct a loop, we may use e = make_edge().rot();
// then we will have e Org = e Dest, e Left != e Right, e Lnext = e Rnext = e,
// and e Onext = e Oprev = e Sym.
template <typename T> edge_reference<T> make_edge() {
    quad_edge<T> *q = new quad_edge<T>;
    q->e[0].next = {q, 0}; // e0 Onext = e0
    q->e[1].next = {q, 3}; // e1 Onext = e1 sym = e0 rot3
    q->e[2].next = {q, 2}; // e2 Onext = e2     = e0 rot2
    q->e[3].next = {q, 1}; // e3 Onext = e3 sym = e0 rot1
    return {q, 0};         // canonical edge reference
}

// From Guibas & Stolfi:
// This operation affects the two edge rings a Org and b Org and, independently,
// the two edge rings a Left and b Left. In each case, (a) If the two rings are
// distinct, Splice will combine them into one; (b) if the two are exactly the
// same ring, Splice will break it in two separate pieces; (c) if the two are
// the same ring taken with opposite orientations, Splice will Flip (and reverse
// the order) of a segment of that ring. The effect of this operation is not
// defined if a is a primal edge and b is dual, or vice-versa.
template <typename T> void splice(edge_reference<T> a, edge_reference<T> b) {

    edge_reference<T> alph = a.o_next().rot();
    edge_reference<T> beta = b.o_next().rot();

    edge_reference<T> a_o_temp = a.o_next();
    edge_reference<T> b_o_temp = b.o_next();
    edge_reference<T> alph_o_temp = alph.o_next();
    edge_reference<T> beta_o_temp = beta.o_next();

    a.q->e[a.r].next = b_o_temp;          // a Onext <- b Onext
    b.q->e[b.r].next = a_o_temp;          // b Onext <- a Onext
    alph.q->e[alph.r].next = beta_o_temp; // alph Onext <- beta Onext
    beta.q->e[beta.r].next = alph_o_temp; // beta Onext <- alph Onext
}

#define ORG data()
#define DEST sym().data()

// Add a new edge e connecting the destination of a to the origin of b, in such
// a way that a Left = e Left = b Left after the connection is complete. For
// added convenience it will also set the Org and Dest fields of the new edge
// to a.Dest and b.Org, respectively.
template <typename T> edge_reference<T> connect(edge_reference<T> a, edge_reference<T> b) {
    edge_reference e = make_edge<T>();
    e.ORG = a.DEST;
    e.DEST = b.ORG;
    splice(e, a.l_next());
    splice(e.sym(), b);
    return e;
}

// Disconnect the edge e from the rest of the structure (this may cause the rest
// of the structure to fall apart in two separate components) and free the
// associated quad_edge. In a sense, delete_edge is the inverse of connect.
template <typename T> void delete_edge(edge_reference<T> &e) {
    splice(e, e.o_prev());
    splice(e.sym(), e.sym().o_prev());
    delete e.q;
    e.q = nullptr;
}

// Delete the entire structure of the graph connected to edge e.
template <typename T> void kill_graph(edge_reference<T> e) {
    auto a = e.o_next();
    auto b = e.d_next();
    bool del_a = a.q != e.q;
    bool del_b = b.q != e.q;
    delete_edge(e);
    if (del_a)
        kill_graph(a);
    if (del_b)
        kill_graph(b);
}

namespace std {
template <typename T> struct hash<edge_reference<T>> {
    std::size_t operator()(const edge_reference<T> &e) const noexcept {
        // hash collisions should never happen since e.r never exceeds 3,
        // which will be less than the size allocated for a quad_edge.
        return ((std::size_t)e.q) + (e.r);
    }
};
} // namespace std
