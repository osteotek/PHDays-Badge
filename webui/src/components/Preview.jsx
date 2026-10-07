import React from 'react';
import { generatePixelDrawCss, generateAnimationCSSData } from '../utils/cssParse';
import Animation from './Animation';

const Preview = props => {
  const generatePreview = () => {
    const { activeFrameIndex, duration, storedData, animationName } = props;
    const { frames, columns, cellSizeShow, animate } = storedData || props;
    const animation = frames.size > 1 && animate;
    let animationData;
    let cssString;

    const styles = {
      previewWrapper: {
        height: cellSizeShow,
        width: cellSizeShow,
        position: 'absolute',
        top: '-5px',
        left: '-5px'
      }
    };

    if (animation) {
      animationData = generateAnimationCSSData(frames, columns, cellSizeShow);
    } else {
      cssString = generatePixelDrawCss(frames.get(activeFrameIndex), columns, cellSizeShow, 'string');

      styles.previewWrapper.boxShadow = cssString;
      styles.previewWrapper.MozBoxShadow = cssString;
      styles.previewWrapper.WebkitBoxShadow = cssString;
    }

    return (
      <div style={animation ? null : styles.previewWrapper}>
        {animation ? <Animation duration={duration} boxShadow={animationData} name={animationName} /> : null}
      </div>
    );
  };

  const { storedData } = props;
  const { columns, rows, cellSizeShow } = storedData || props;
  const style = {
    width: columns * cellSizeShow,
    height: rows * cellSizeShow,
    position: 'relative'
  };

  return (
    <div className="preview" style={style}>
      {generatePreview()}
    </div>
  );
};
export default Preview;
