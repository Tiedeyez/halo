#pragma once

#include <QColor>
#include <QImage>
#include <QPointF>
#include <QQuickPaintedItem>
#include <QStringList>
#include <array>
#include <vector>

class Spectrum;

// Cymatics, worked out rather than drawn: a round metal plate, free at its
// edge, shaken by the music, with sand on it.
//
// THE PLATE. A thin plate obeys the Kirchhoff-Love equation
//     D lap(lap(w)) + rho h d2w/dt2 = F,
// and a free circular one rings in the modes
//     w(r, theta) = [J_n(L r) + C I_n(L r)] cos(n theta),
// J_n and I_n the Bessel functions, n the number of nodal diameters. L and C
// come from the free edge (no bending moment, no shear across it), which
// gives the characteristic equation solved in Plate::eigenvalues(); the
// mode's pitch rises as L squared. The numbers are checked in the tests
// against the published table (Leissa, Vibration of Plates, table 2.5).
//
// THE MUSIC. Each mode is a resonator: a damped oscillator that answers the
// part of the spectrum at its own pitch, quickly (a treble mode rings up in
// milliseconds, a bass one in tens), so the plate follows the music note by
// note. Modes of different pitch do not interfere over time, so the plate's
// mean motion is the sum of each mode's own: E = sum a_m^2 w_m^2.
//
// THE SAND. A grain on a shaking plate is thrown where the plate moves and
// lies where it is still, so it walks at random, further where E is large,
// and drifts down E's slope until it finds a place that stays still: the
// nodal lines. Below the loudness that lifts a grain off the plate nothing
// moves at all. Every beat is a blow to the whole plate and throws all of it.
// Sand takes up room: a place that is full pushes more away, with a
// pressure that is nothing until the sand is heaped and steep after (the
// fourth power of how full), so the grains fill the plate's quiet valleys
// like water fills a basin instead of all standing on one point.
//
// THE MANDALA. The modes used share a number of nodal diameters (0, F and
// 2F), so the plate's motion repeats F times round the centre and mirrors
// across each spoke, and the sand's does too: it is counted and drawn in
// one wedge and its 2F images. F comes from the song.
namespace Plate {

constexpr double Poisson = 0.33;   // the plate's Poisson's ratio (aluminium, brass)
constexpr int Samples = 385;       // points along a radius in a mode's table

// Zero where L is an eigenvalue of the free plate with n nodal diameters.
double characteristic(int n, double lambda, double nu = Poisson);
// The first `count` eigenvalues L, lowest first, without the two motions
// that are not vibration (the plate moving or tilting as a whole).
std::vector<double> eigenvalues(int n, int count, double nu = Poisson);

struct Mode {
    int n = 0;           // nodal diameters
    int ring = 0;        // which of that n's eigenvalues, 0 first
    int harmonic = 0;    // n as a multiple of the fold: 0, 1 or 2
    double lambda = 0;
    double hertz = 0;    // its pitch on CymtX's plate
    std::vector<float> shape;   // along the radius, largest value 1
    std::vector<float> slope;   // its derivative there
};
Mode mode(int n, int ring, double lambda, double nu = Poisson);

// The pitch of the plate's lowest ring mode (n = 0), which sets every other.
constexpr double LowestRingHertz = 55.0;

struct Grain {
    float x = 0, y = 0;   // the plate is the unit disc
    float heat = 0;       // how fast it has been moving lately, 0..1
};

class Sand {
public:
    Sand();
    // Nodal diameters the mandala repeats in: 3 to 12.
    void setFold(int fold);
    int fold() const { return m_fold; }
    void setGrains(int count);
    const std::vector<Grain> &grains() const { return m_grains; }
    const std::vector<Mode> &modes() const { return m_modes; }
    // How hard each mode is ringing, 0..1.
    const std::vector<double> &ringing() const { return m_ringing; }

    // dt seconds; the spectrum's 64 bands (0..1, decibel scale); the beat's
    // pulse and the loudness, 0..1.
    void step(double dt, const std::vector<float> &bands, double pulse, double level);
    // A knock on the plate at (x, y): the sand round it jumps.
    void tap(double x, double y, double force);
    // The plate's mean motion at a point, 0..1, given cos and sin of F theta.
    double motion(double r, double cosF, double sinF) const;
    double kick() const { return m_kick; }
    double drive() const { return m_drive; }

private:
    void field(double r, double cosF, double sinF, double &e, double &dr, double &dtheta) const;
    float random01();
    float randomNormal();

    int m_fold = 6;
    std::vector<Mode> m_modes;
    std::vector<double> m_ringing, m_usual, m_spread, m_weight;
    // The plate's motion along a radius for each harmonic (0, F, 2F), and
    // its slope, summed over that harmonic's modes once a frame: a grain or
    // a cell then needs three lookups, not one per mode.
    std::array<std::vector<float>, 3> m_profile, m_profileSlope;
    void buildProfiles();
    // How much sand lies where, counted in the mandala's own coordinates:
    // rings along the radius, and the angle folded into one wedge between
    // a spoke and the next mirror line (every image of a grain falls in
    // the same bin, so each is counted once). `m_packed` is how full each
    // bin is, blurred a little, for the pressure.
    std::vector<float> m_count, m_heat, m_nextCount, m_nextHeat, m_packed, m_scratch;
    void countSand();
    void pack();
    double fullCount(int ring) const;

public:
    static constexpr int Rings = 192, Wedge = 64;
    // The bin a point belongs to; `turn` (if given) is +1 or -1, the way
    // the wedge's angle runs there against theta.
    int bin(double r, double theta, int *turn = nullptr) const;
    // Sand in a bin, 0 (none) to 1 (heaped) and beyond, and how much of it
    // is in the air, 0..1.
    double sandIn(int bin) const { return m_count[size_t(bin)] / fullCount(bin / Wedge); }
    double heatIn(int bin) const { return m_count[size_t(bin)] > 0 ? m_heat[size_t(bin)] / m_count[size_t(bin)] : 0; }

private:
    std::vector<Grain> m_grains;
    double m_floor = 0, m_kick = 0, m_drive = 0;
    bool m_armed = true;
    unsigned m_seed = 0x9E3779B9u;
};

}

// The plate as an item: one cell of the image per square pixel on screen
// (Cymatics.qml enlarges it). `vivid` is the Cymatics visual, bright, with
// the plate's own motion glowing under the sand; otherwise it is the
// window's background: sand only, in the accent, no brighter than
// `strength` (so the faintest text over it reads at 7:1) except for the
// moment of a beat (`flash`).
class SandPlate : public QQuickPaintedItem {
    Q_OBJECT
    Q_PROPERTY(QObject *spectrum READ spectrum WRITE setSpectrum NOTIFY changed)
    Q_PROPERTY(QStringList palette READ palette WRITE setPalette NOTIFY changed)
    Q_PROPERTY(bool vivid READ vivid WRITE setVivid NOTIFY changed)
    Q_PROPERTY(double strength READ strength WRITE setStrength NOTIFY changed)
    Q_PROPERTY(double flash READ flash WRITE setFlash NOTIFY changed)
    // The plate's centre and radius, in cells.
    Q_PROPERTY(QPointF centre READ centre WRITE setCentre NOTIFY changed)
    Q_PROPERTY(double radius READ radius WRITE setRadius NOTIFY changed)
    Q_PROPERTY(int grains READ grains WRITE setGrains NOTIFY changed)
    Q_PROPERTY(int fold READ fold NOTIFY changed)

public:
    explicit SandPlate(QQuickItem *parent = nullptr);
    void paint(QPainter *painter) override;

    QObject *spectrum() const;
    void setSpectrum(QObject *spectrum);
    QStringList palette() const { return m_paletteNames; }
    void setPalette(const QStringList &palette);
    bool vivid() const { return m_vivid; }
    void setVivid(bool vivid);
    double strength() const { return m_strength; }
    void setStrength(double strength);
    double flash() const { return m_flash; }
    void setFlash(double flash);
    QPointF centre() const { return m_centre; }
    void setCentre(const QPointF &centre);
    double radius() const { return m_radius; }
    void setRadius(double radius);
    int grains() const { return int(m_sand.grains().size()); }
    void setGrains(int grains);
    int fold() const { return m_sand.fold(); }

    // The song picks the mandala's fold, the same every time it plays.
    Q_INVOKABLE void setSong(const QString &path, const QString &title);
    static int foldFor(const QString &path, const QString &title);
    Q_INVOKABLE void step(double dt, double pulse, double level);
    // A click at a cell.
    Q_INVOKABLE void tap(double x, double y, double force);

    const QImage &frame() const { return m_image; }
    Plate::Sand &sand() { return m_sand; }

signals:
    void changed();

private:
    void ensureSize();
    void render();

    Spectrum *m_spectrum = nullptr;
    Plate::Sand m_sand;
    QStringList m_paletteNames;
    std::array<QColor, 6> m_palette;
    bool m_vivid = false;
    double m_strength = 0.1, m_flash = 0.4;
    QPointF m_centre{0, 0};
    double m_radius = 40;
    QImage m_image;
    // Each cell's place on the plate (its radius, cos and sin of F theta,
    // and the sand bin it shows), worked out when the plate moves or
    // resizes, not every frame.
    std::vector<int> m_cellBin;
    std::vector<quint8> m_binLook;
    bool m_cellsStale = true;
    void placeCells();
    std::vector<float> m_quiet;   // a silent spectrum, for when none is attached
};
