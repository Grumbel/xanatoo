# TeX math

Math is written the way Wikipedia and most Markdown tools do: `$…$` sets a
formula in the text, `$$…$$` on a line of its own sets it centred, like
$e^{i\pi} + 1 = 0$ here. The formulas are laid out by xanatoo itself from the
OpenType MATH table of a math font (Latin Modern Math, STIX Two Math, …).

## Display formulas

$$x = \frac{-b \pm \sqrt{b^2 - 4ac}}{2a}$$

$$\sum_{n=1}^{\infty} \frac{1}{n^2} = \frac{\pi^2}{6}$$

$$\int_{-\infty}^{\infty} e^{-x^2}\,dx = \sqrt{\pi}$$

$$\nabla \times \mathbf{B} = \mu_0 \mathbf{J} + \mu_0 \varepsilon_0 \frac{\partial \mathbf{E}}{\partial t}$$

$$\begin{pmatrix} a & b \\ c & d \end{pmatrix}
\begin{pmatrix} x \\ y \end{pmatrix} =
\begin{pmatrix} ax + by \\ cx + dy \end{pmatrix}$$

$$f(x) = \begin{cases} x^2 & \text{if } x \ge 0 \\ -x & \text{otherwise} \end{cases}$$

$$\begin{aligned}
(a+b)^2 &= a^2 + 2ab + b^2 \\
        &= a^2 + b^2 + 2ab
\end{aligned}$$

## In running text

The Gaussian $\mathcal{N}(\mu, \sigma^2)$ has density
$\frac{1}{\sigma\sqrt{2\pi}} e^{-(x-\mu)^2 / 2\sigma^2}$, and for
$z \in \mathbb{C}$ with $|z| < 1$ the geometric series gives
$\sum_{k=0}^\infty z^k = \frac{1}{1-z}$. A price of $5 or $6 stays text.

## Notation

Letters $a, b, \alpha, \beta, \Gamma, \Omega$; alphabets
$\mathbb{R}\ \mathcal{L}\ \mathfrak{g}\ \mathbf{v}\ \mathrm{d}x$;
accents $\hat{x}\ \bar{y}\ \vec{v}\ \widetilde{abc}$; relations
$\le\ \ne\ \approx\ \equiv\ \in\ \subset\ \to\ \Rightarrow$; operators
$\lim_{x\to 0}\ \sin\ \log\ \max$; big delimiters
$\left( \frac{a}{b} \right)\ \left\{ x \right\}\ \left\langle \psi \right|$.

Source: write `$` or `$$` around TeX; a `\$` is a dollar sign.
