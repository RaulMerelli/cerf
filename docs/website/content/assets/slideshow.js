/* Device tiles cycle through the screenshots in their folder. The slide list is
   emitted by hooks/render.py as data-cerf-slides on the tile's <img>.

   No transition on the swap: with a wall of tiles cycling at once, any fade
   reads as flicker across the page. The images are preloaded, so the swap is a
   single clean repaint. */
(function () {
  var HOLD_MS = 7000;

  var timers = [];

  function play(img) {
    var slides = [img.getAttribute('src')].concat(
      img.getAttribute('data-cerf-slides').split(' ').filter(Boolean));
    if (slides.length < 2) return;

    slides.forEach(function (src) { new Image().src = src; });

    var offset = parseInt(img.getAttribute('data-cerf-offset'), 10) || 0;
    var i = 0;

    timers.push(setTimeout(function () {
      timers.push(setInterval(function () {
        if (document.hidden) return;
        i = (i + 1) % slides.length;
        img.src = slides[i];
      }, HOLD_MS));
    }, offset));
  }

  function stop() {
    timers.forEach(function (t) { clearTimeout(t); clearInterval(t); });
    timers = [];
  }

  function arm() {
    stop();
    if (matchMedia('(prefers-reduced-motion: reduce)').matches) return;
    document.querySelectorAll('img[data-cerf-slides]').forEach(play);
  }

  if (typeof document$ !== 'undefined') {
    document$.subscribe(arm);
  } else {
    document.addEventListener('DOMContentLoaded', arm);
  }
})();
