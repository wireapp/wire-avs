// Global variable initialization
buildNumber = currentBuild.id
version = null
release_version = ""
branchName = ""
commitId = null
repoName = null
changelog = ""

pipeline {
    agent none

    options {
        parallelsAlwaysFailFast()
        disableConcurrentBuilds()
    }

    // NOTE: checks every 5 minutes if a new commit occurred after last successful run
    triggers {
        pollSCM 'H/5 * * * *'
    }

    stages {
       stage('Test + Build') {
            parallel {
                stage('Linux') {
                    agent {
			  dockerfile {
                            filename 'Dockerfile'
                            // Explicitly force the path to look inside common cargo locations
                            //args '-v /home/jenkins/workspace:/workspace --env PATH=/usr/share/cargo/bin:/build/avs/.cargo/bin:/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin'
			    additionalBuildArgs '--no-cache'
                        }
                    }
                    steps {
		        script {
		           def vcs = checkout([
		              	$class: 'GitSCM',
		              	changelog: true,
		              	userRemoteConfigs: scm.userRemoteConfigs,
		              	branches: scm.branches,
		              	extensions: scm.extensions + [
			          [$class: 'SubmoduleOption', disableSubmodules: false, recursiveSubmodules: true, parentCredentials: true]
		              	]
		   	   ])
                  	branchName = vcs.GIT_BRANCH
                  	commitId = "${vcs.GIT_COMMIT}"[0..6]
                  	repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                  	release_version = branchName.replaceAll("[^\\d\\.]", "")
                  	if (release_version.length() > 0 || branchName.contains('release')) {
                     		version = release_version + "." + buildNumber
                  	} else {
                     		version = "0.0.${buildNumber}"
                  	}
	        	}		   
			
			sh 'make distclean || true'
			sh '''
			   # Blast away cached dependency metadata from previous container runs
			   rm -rf /build/avs/.cargo/registry/cache/
			   rm -rf /build/avs/.cargo/registry/src/
			   cargo clean || true
			'''						
                        sh 'touch src/version/version.c'			

                        // build tests
                        sh 'make test BUILD_OPTIONAL_MODULES=1 HAVE_PROTOBUF=1 HAVE_CRYPTOBOX=1 AVS_VERSION=' + version
                        // run tests
                        sh './ztest'
                        // run slow tests
                        sh './ztest-slow || true'

                        // cleanup old artifacts
                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'

                        // build
                        sh 'make avs_clean dist_clean'
                        sh 'make zcall sectest AVS_VERSION=' + version
                        script {
                            def exitStatus = sh returnStatus: true, script: './sectest https://sft.calling-staging-v01.zinfra.io:443 > ./build/artifacts/avs-' + version + '-sectest.log'
                            if (exitStatus != 0) {
                                sh 'cat ./build/artifacts/avs-' + version + '-sectest.log'
                                error('sectest failed')
                            }
                        }
                        sh '''#!/bin/bash
                            . ./scripts/android_devenv.sh && make dist_linux dist_android AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''
                        sh 'cp ./build/dist/linux/avscore.tar.bz2 ./build/artifacts/avs.linux.' + version + '.tar.bz2'
                        sh 'zip -9j ./build/artifacts/avs.android.' + version + '.zip ./build/dist/android/avs.aar'
                        sh 'zip -9j ./build/artifacts/zcall_linux_' + version + '.zip ./zcall'
                        sh 'if [ -e ./build/dist/android/debug/ ]; then cd ./build/dist/android/debug; zip -9r ./../../../artifacts/avs.android.' + version + '.debug.zip *; cd -; fi'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false

                        // Stash the android aar directory recursively,
                        // shared libraries will be used to generate android kmp in macos agent
                        stash name: 'android-aar', includes: 'build/dist/android/aar/**,build/dist/android/avs.aar'
                    }
                }
                stage('macOS') {
                    agent {
                        label 'macos'
                    }
                    environment {
                        PATH = "/opt/homebrew/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${env.PATH}"
                    }
                    steps {
                        script {
                            def vcs = checkout([
                                    $class: 'GitSCM',
                                    changelog: true,
                                    userRemoteConfigs: scm.userRemoteConfigs,
                                    branches: scm.branches,
                                    extensions: scm.extensions + [
                                            [
                                            $class: 'SubmoduleOption',
                                            disableSubmodules: false,
                                            recursiveSubmodules: true,
                                            parentCredentials: true
                                            ]
                                    ]
                            ])

                            branchName = vcs.GIT_BRANCH
                            commitId = "${vcs.GIT_COMMIT}"[0..6]
                            repoName = vcs.GIT_URL.tokenize( '/' ).last().tokenize( '.' ).first()

                            release_version = branchName.replaceAll("[^\\d\\.]", "");
                            if (release_version.length() > 0 || branchName.contains('release')) {
                                version = release_version + "." + buildNumber
                            } else {
                                version = "0.0.${buildNumber}"
                            }
                        }

                        // clean
                        sh 'make distclean'
			sh '''
			   # Blast away cached dependency metadata from previous container runs
			   rm -rf /build/avs/.cargo/registry/cache/
			   rm -rf /build/avs/.cargo/registry/src/
			   cargo clean || true
			'''			
                        sh 'touch src/version/version.c'

                        // build tests
                        sh 'make test BUILD_OPTIONAL_MODULES=1 HAVE_PROTOBUF=1 HAVE_CRYPTOBOX=1 AVS_VERSION=' + version
                        // run tests
                        sh './ztest'
                        // run slow tests
                        sh './ztest-slow'

                        // build
                        sh 'make avs_clean dist_clean'
                        sh 'make zcall AVS_VERSION=' + version
                        sh '''#!/bin/bash
                            . ./scripts/android_devenv.sh && echo "sdk.dir=${ANDROID_SDK_ROOT}\nndk.dir=${ANDROID_NDK_ROOT}" > local.properties
                            . ./scripts/wasm_devenv.sh && make dist_xc dist_wasm AVS_VERSION=''' + version + '  BUILDVERSION=' + version + '''
                        '''

                        sh 'rm -rf ./build/artifacts'
                        sh 'mkdir -p ./build/artifacts'
                        sh 'cp ./build/dist/osx/avs.framework.zip ./build/artifacts/avs.framework.osx.' + version + '.zip'
                        sh 'cp ./build/dist/xc/avs.xcframework.zip ./build/artifacts/avs.xcframework.zip'
                        sh 'zip -9j ./build/artifacts/zcall_osx_' + version + '.zip ./zcall'
                        sh 'mkdir -p ./osx'
                        sh 'cp ./build/dist/osx/avscore.tar.bz2 ./osx'
                        sh 'cp ./build/dist/wasm/wireapp-avs-' + version + '.tgz ./build/artifacts/'

                        archiveArtifacts artifacts: 'build/artifacts/*', followSymlinks: false
                    }
                }
            }
        }
        stage('Prepare changelog') {
            steps {
                script {
                    currentBuild.changeSets.each { set ->
                        set.items.each { entry ->
                            changelog += '- ' + entry.msg + '\n'
                        }
                    }
                }
                echo("Changelog:")
                echo(changelog)
            }
        }
        stage('Tag + Create Github release') {
            agent {
                label "linuxbuild"
            }
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }

            steps {
                echo "Tag as ${version}"
                withCredentials([sshUserPrivateKey(credentialsId: 'wire-avs', keyFileVariable: 'sshPrivateKeyPath')]) {
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"

                            git tag ${version}

                            git \
                                -c core.sshCommand='ssh -i ${sshPrivateKeyPath}' \
                                push \
                                origin ${version}
                        """
                    )
                }
                echo 'Creating release on Github'
                // Unfortunately it is not allowed to access github API with a deploy key so we have to rely on
                // a user token to upload via python github package
                withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                    string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                    // NOTE: creating an empty stub directory just to create the release
                    sh(
                        script: """
                            cd "${env.WORKSPACE}"
                            GITHUB_USER=${repoUser} \
                            GITHUB_TOKEN=${accessToken} \
			    python3 ./scripts/release-on-github.py \
                                ${repoName} \
                                \$(mktemp -d) \
                                ${version} \
                                "${changelog}"
                        """
                    )
                }
            }
        }
        stage('Upload to Github') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') || "${branchName}".contains('main') }
                }
            }
            matrix {
                axes {
                    axis {
                        name 'AGENT'
                        values 'macos', 'linuxbuild'
                    }
                }
                agent {
                    label "${AGENT}"
                }

                stages {
                    stage( 'Uploading release artifacts' ) {
                        steps {
                            withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                                string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                                sh(
                                    script: """
                                        sleep 5
                                        GITHUB_USER=${repoUser} \
                                        GITHUB_TOKEN=${accessToken} \
                                        python3 ./scripts/release-on-github.py \
                                            ${repoName} \
                                            ./build/artifacts \
                                            ${version} \
                                            "${changelog}"
                                    """
                                )
                            }
                        }
                    }
                }
            }
        }
        stage('Publish to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'linuxbuild'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                script {
                    echo '### Publish MavenLocal for Wire S3 upload'
                    withCredentials([
                            string(credentialsId: 'sonatype-signing-key-password', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKeyPassword'),
                            string(credentialsId: 'sonatype-signing-key', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKey')
                        ]) {
                        sh(
                            script: """
                                ORG_GRADLE_PROJECT_VERSION_NAME=$version ./gradlew publishMavenJavaPublicationToMavenLocal
                                mkdir -p ./build/artifacts/maven/com/wire
                                cp -r ~/.m2/repository/com/wire/avs ./build/artifacts/maven/com/wire/
                            """
                        )
                    }
                    echo '### Attach MavenLocal artifacts to GitHub release'
                    withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                        string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                        sh(
                            script: """
                                GITHUB_USER=${repoUser} \\
                                GITHUB_TOKEN=${accessToken} \\
                                python3 ./scripts/release-on-github.py \\
                                    ${repoName} \\
                                    ./build/artifacts/maven \\
                                    ${version} \\
                                    "MavenLocal artifacts for ${version}"
                            """
                        )
                    }
                    echo '### Upload to Wire S3 Maven repository'
                    withCredentials([
                        usernamePassword(
                            credentialsId: 's3_package_key',
                            usernameVariable: 'AWS_ACCESS_KEY_ID',
                            passwordVariable: 'AWS_SECRET_ACCESS_KEY'
                        )
                    ]) {
                        sh(
                            script: """
                                cd "${env.WORKSPACE}"
                                # Install AWS CLI v2 if not present
                                if ! command -v aws &> /dev/null; then
                                    curl -s "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
                                    rm -rf aws || true
                                    unzip -q awscliv2.zip
                                    ./aws/install --bin-dir "$HOME/.local/bin" --install-dir "$HOME/.local/aws-cli" --update
                                fi
                                export PATH="$HOME/.local/bin:$PATH"
                                # Gradle generates maven-metadata-local.xml, rename to maven-metadata.xml for S3 upload
                                find ./build/artifacts/maven -name 'maven-metadata-local.xml' -exec sh -c 'for f; do mv "$f" "\${f%-local.xml}.xml"; done' _ {} +
                                echo "Uploading Maven artifacts to s3://maven-wire-com..."
                                aws s3 cp ./build/artifacts/maven s3://maven-wire-com/ \\
                                    --recursive \\
                                    --no-overwrite \\
                                    --region us-east-1 \\
                                    --exclude '*maven-metadata.xml'
                                aws s3 cp ./build/artifacts/maven s3://maven-wire-com/ \\
                                    --recursive \\
                                    --region us-east-1 \\
                                    --exclude '*' \\
                                    --include '*maven-metadata.xml'
                                echo "Maven artifacts published to s3://maven-wire-com"
                            """
                        )
                    }
                }
            }
        }

        // WPB-24450 macos agent is handling kmp publish
        // When migration is complate, 'Publish to sonatype' legacy android step can be removed.
        stage('Publish kmp to sonatype') {
            when {
                anyOf {
                    expression { return "${branchName}".contains('release') }
                }
            }
            agent {
                label 'macos'
            }
            environment {
                PATH = "/opt/homebrew/bin:/Applications/Xcode.app/Contents/Developer/usr/bin:/Users/jenkins/.cargo/bin:/usr/local/bin:${ env.PATH }"
            }
            steps {
                // Restore the android shared libraries generated in linux agent
                unstash 'android-aar'

                script {
                    echo '### Publish MavenLocal for Wire S3 upload'
                    withCredentials([
                            string(credentialsId: 'sonatype-signing-key-password', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKeyPassword'),
                            string(credentialsId: 'sonatype-signing-key', variable: 'ORG_GRADLE_PROJECT_signingInMemoryKey')
                        ]) {
                        sh(
                            script: """
                                mkdir -p ./build/artifacts/maven/com/wire
                                ORG_GRADLE_PROJECT_VERSION_NAME=$version ./gradlew :avs-kmp:publishToMavenLocal --no-configuration-cache
                                cp -r ~/.m2/repository/com/wire/avs-kmp ./build/artifacts/maven/com/wire/
                            """
                        )
                    }
                    echo '### Attach MavenLocal artifacts to GitHub release'
                    withCredentials([ string( credentialsId: 'github-repo-user', variable: 'repoUser' ),
                        string( credentialsId: 'github-repo-access', variable: 'accessToken' ) ]) {
                        sh(
                            script: """
                                GITHUB_USER=${repoUser} \\
                                GITHUB_TOKEN=${accessToken} \\
                                python3 ./scripts/release-on-github.py \\
                                    ${repoName} \\
                                    ./build/artifacts/maven \\
                                    ${version} \\
                                    "MavenLocal artifacts for ${version}"
                            """
                        )
                    }
                    echo '### Upload to Wire S3 Maven repository'
                    withCredentials([
                        usernamePassword(
                            credentialsId: 's3_package_key',
                            usernameVariable: 'AWS_ACCESS_KEY_ID',
                            passwordVariable: 'AWS_SECRET_ACCESS_KEY'
                        )
                    ]) {
                        sh(
                            script: """
                                cd "${env.WORKSPACE}"
                                # Install AWS CLI v2 if not present
                                if ! command -v aws &> /dev/null; then
                                    curl -s "https://awscli.amazonaws.com/awscli-exe-linux-x86_64.zip" -o "awscliv2.zip"
                                    rm -rf aws || true
                                    unzip -q awscliv2.zip
                                    ./aws/install --bin-dir "$HOME/.local/bin" --install-dir "$HOME/.local/aws-cli" --update
                                fi
                                export PATH="$HOME/.local/bin:$PATH"
                                # Gradle generates maven-metadata-local.xml, rename to maven-metadata.xml for S3 upload
                                find ./build/artifacts/maven -name 'maven-metadata-local.xml' -exec sh -c 'for f; do mv "$f" "\${f%-local.xml}.xml"; done' _ {} +
                                echo "Uploading Maven artifacts to s3://maven-wire-com..."
                                aws s3 cp ./build/artifacts/maven s3://maven-wire-com/ \\
                                    --recursive \\
                                    --no-overwrite \\
                                    --region us-east-1 \\
                                    --exclude '*maven-metadata.xml'
                                aws s3 cp ./build/artifacts/maven s3://maven-wire-com/ \\
                                    --recursive \\
                                    --region us-east-1 \\
                                    --exclude '*' \\
                                    --include '*maven-metadata.xml'
                                echo "Maven artifacts published to s3://maven-wire-com"
                            """
                        )
                    }
                }
            }
        }

//        stage('Publish to ios github repo') {
//            when {
//                anyOf {
//                    expression { return "${branchName}".contains('release') }
//                }
//            }
//            agent {
//                label 'built-in'
//            }
//            steps {
//                withCredentials([ string( credentialsId: 'ios-github', variable: 'accessToken' ) ]) {
//                    sh """
//                        GITHUB_TOKEN=${ accessToken } \
//                        python3 ./scripts/upload-ios.py \
//                            ./build/artifacts/avs.xcframework.zip \
//                            ${version} \
//                            appstore
//                    """
//                }
//            }
//        }
    }

    post {
        always {
            node('linuxbuild') {
                script {
                    sh 'docker container prune -f && docker volume prune -f && docker image prune -f'
		    sh 'docker system prune -af --volumes'
                }
            }
        }

        success {
            node( 'macos' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "✅ ${JOB_NAME} #${BUILD_ID} succeeded\n**Changelog:**\n${changelog}\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }

        failure {
            node( 'macos' ) {
                withCredentials([ string( credentialsId: 'wire-jenkinsbot', variable: 'jenkinsbot_secret' ) ]) {
                    wireSend secret: "$jenkinsbot_secret", message: "❌ ${JOB_NAME} #${BUILD_ID} failed\n${BUILD_URL}console\nhttps://github.com/wireapp/wire-avs/commit/${commitId}"
                }
            }
        }
    }
}
