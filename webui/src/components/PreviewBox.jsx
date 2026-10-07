import React, { useState } from 'react';
import { useSelector } from 'react-redux';
import Preview from './Preview';
import DurationContainer from './Duration';

const PreviewBox = props => {
  const [animate, setAnimate] = useState(false);
  const frames = useSelector(state => state.present.get('frames'));
  const duration = useSelector(state => state.present.get('duration'));
  const frameList = frames.get('list');
  const activeFrameIndex = frames.get('activeIndex');
  const columns = frames.get('columns');
  const rows = frames.get('rows');
  const { helpOn } = props;
  const animMessage = `${animate ? 'Pause' : 'Play'} the animation`;
  const animTooltip = helpOn ? animMessage : null;
  const bgPixelSize = 16;

  return (
    <div className="preview-box">
      <div className="buttons">
        <div data-tooltip={animTooltip}>
          <button
            type="button"
            className={animate ? 'pause' : 'play'}
            onClick={() => setAnimate(!animate)}
            aria-label="Animation control"
          />
        </div>
        <DurationContainer />
      </div>
      <div className="preview-box__container">
        <Preview
          frames={frameList}
          columns={columns}
          rows={rows}
          cellSize={bgPixelSize}
          cellSizeShow={bgPixelSize}
          duration={duration}
          activeFrameIndex={activeFrameIndex}
          animate={animate}
          animationName="wip-animation"
        />
      </div>
    </div>
  );
};

export default PreviewBox;
