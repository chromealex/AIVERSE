# 3D-модели в UI

`ui::UiModelView` (`engine/ui/UiModelView.h`) рисует одну модель в текстуру, доступную RmlUi как `<img src="#model:NAME"/>`. Для каждого изображения вызовите `init(device, ui, name, width, height)`, рисуйте вне открытого графического прохода до `UiLayer::render()` и вызовите `shutdown()` до уничтожения устройства или UI. Размер — от 16 до 2048 пикселей. Компонент владеет своим рендерером и целевыми текстурами; модели, материалы, анимация и entity сцены остаются у вызывающей стороны.

Для объекта сцены передайте его готовый `render::ModelDraw` и камеру в `renderDraw`: сохраняются модель, материалы, трансформация и текущая поза. Для отдельного анимированного объекта создайте собственный презентационный `render::Animator`, обновляйте его один раз за кадр и вызывайте `renderAnimator(model, animator, camera)`. Если позу уже вычислил MotionWeave, передайте её в `renderPose`. UI не меняет авторитетный ECS и не обновляет анимацию объекта сцены второй раз.

```cpp
ui::UiModelView icon;
icon.init(device, uiLayer, "selected", 256, 256);
icon.renderDraw(draw, portraitCamera); // draw — текущий пакет entity сцены
// RmlUi: <img src="#model:selected" />

render::Animator separateAnimation;
separateAnimation.setModel(&model);
// После настройки клипа через Animator::play, каждый кадр:
separateAnimation.update(deltaSeconds);
icon.renderAnimator(model, separateAnimation, portraitCamera);
```

Модель и все указатели в пакете должны оставаться действительными до завершения синхронного вызова. Рисование в текстуру и чтение её в RmlUi выполняются в разных проходах. Освобождение ресурсов подчиняется завершению работы GPU. Камеру и ориентацию изображения для своего графического бэкенда выбирает хост.
