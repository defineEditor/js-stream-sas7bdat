import DatasetSas7BDat from '../src/index';
import path from 'path';

test('Get metadata', async () => {
    const filePath = path.join(__dirname, '/data/sample.sas7bdat');

    const data = new DatasetSas7BDat(filePath);
    const metadata = await data.getMetadata();
    expect(metadata).toMatchSnapshot();
});